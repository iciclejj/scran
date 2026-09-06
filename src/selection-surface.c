#include <math.h>

#include <blend2d/blend2d.h>

#include "selection.h"
#include "state.h"
#include "selection-surface.h"
#include "init.h"
#include "util/blend2d.h"
#include "event-handlers.h"
#include "ui.h"
#include "print.h"


static inline void
draw_and_damage_region(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    // Wayland needs to be damaged with difference with the previously drawn box,
    // regardless of which buffer produced it. The buffer itself, on the other
    // hand, must be damaged relative to itself. Marking Wayland damage does
    // not guarantee that no other part of the buffer gets blitted, which is
    // why we must make this distinction.
    //
    // The relevant wl_surface::damage_buffer requirements stated by the xml
    // spec, and with no further guarantees being made, is:
    //   "This request is used to describe the regions **where the pending
    //    buffer is different from the current surface contents** [...]".
    BLRectI damage_region_wayland,
    BLRectI damage_region_buffer
) {
    // TODO: Is bl_context_clear_all the same as bl_context_clear_rect_i, if we do it after clipping?
    bl_context_clip_to_rect_i(&st_buffer->bl_ctx, &damage_region_buffer);
    bl_context_clear_all(&st_buffer->bl_ctx);
    bl_context_fill_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &selection_surface->bl_path);
    bl_context_restore_clipping(&st_buffer->bl_ctx);

    wl_surface_damage_buffer(selection_surface->surface.wl_surface,
        damage_region_wayland.x, damage_region_wayland.y, damage_region_wayland.w, damage_region_wayland.h
    );
}

// NOTE: _draw_and_damage_selection_border must have its damage_regions_buffer
// set accurately (when called after _draw_and_damage_background), since it is
// used to ensure that we don't erase the background we just drew.
// I.e. would not make sense to pass a 'damage_region_everything' even here,
// even as part of a force-redraw.
static inline void
draw_and_damage_selection_border(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area,
    BLBoxI capture_area_border_outline,
    BLBoxI capture_area_border_inline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_inline,  BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);

    for (int i = 0; i < n_damage_regions; ++i) {
        draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    bl_path_clear(&selection_surface->bl_path);

    st_buffer->box_currently_drawn = capture_area;
}

static inline void
draw_and_damage_background(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_max_bounds,
    BLBoxI capture_area_border_outline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions, // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
    bool greeting_screen
) {
    // TODO: Just store the fill styles in state
    BLVarCore prev_fill_style = { };
    bl_context_get_fill_style(&st_buffer->bl_ctx, &prev_fill_style);

    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, SCRAN_SELECTION_BACKGROUND_COLOR.value);

    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_max_bounds,     BL_GEOMETRY_DIRECTION_NONE);
    if (!greeting_screen) { // TODO: likely()
        bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);
    }

    for (int i = 0; i < n_damage_regions; ++i) {
        draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    uint32_t prev_fill_style_rgba32;
    bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);

    bl_path_clear(&selection_surface->bl_path);
}


static inline int
get_total_textline_width_px(
    struct scran_ui_textline_view textline,
    int item_spacing_px
) {
    int total_width_px = 0;

    for (int i = 0; i < textline.n_items; ++i) {
        struct scran_ui_textline_item *item = &textline.items[i];
        if (item->width_px != 0) {
            total_width_px += item->width_px + item_spacing_px;
        }
    }

    total_width_px -= (total_width_px == 0) ? 0 : item_spacing_px;

    return total_width_px;
}


static inline bool
textline_geometry_changed(
    struct scran_ui_textline_geometry *prev,
    struct scran_ui_textline_geometry *new
) {
    return !blpointi_are_equal(prev->origin, new->origin)
        || prev->total_width_px != new->total_width_px;
}

static inline void
draw_and_damage_ui_textline(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    struct scran_ui_textline_view textline,
    int textline_item_spacing_px,
    struct scran_ui_textline_geometry *prev_geometry,
    struct scran_ui_textline_geometry *prev_geometry_any_buffer,
    struct scran_ui_textline_geometry *new_geometry,
    bool textline_changed
) {
    if ( !(textline_changed || st_buffer->force_redraw || textline_geometry_changed(prev_geometry, new_geometry))) {
        return;
    }

    // Clear out old ui
    {
        // TODO: Just store the fill styles in state
        BLVarCore prev_fill_style = { };
        bl_context_get_fill_style(&st_buffer->bl_ctx, &prev_fill_style);
        // XXX TODO: We should probably just be setting this at every render location
        // so we don't have to juggle them like this. Same with fill rule and fill style.
        BLCompOp comp_op = bl_context_get_comp_op(&st_buffer->bl_ctx);
        bl_context_set_comp_op(&st_buffer->bl_ctx, BL_COMP_OP_SRC_COPY);

        BLRectI text_rect_prev = {
            .x = prev_geometry->origin.x,
            .y = prev_geometry->origin.y,
            .w = prev_geometry->total_width_px,
            .h = textline.meta->height_px,
        };

        BLRectI text_rect_prev_any_buffer = {
            .x = prev_geometry_any_buffer->origin.x,
            .y = prev_geometry_any_buffer->origin.y,
            .w = prev_geometry_any_buffer->total_width_px,
            .h = textline.meta->height_px,
        };

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, SCRAN_SELECTION_BACKGROUND_COLOR.value);

        // Uncovered, i.e. minus parts that intrude into capture area/border
        BLRectI text_rect_prev_uncovered[4];
        blboxi_get_difference_as_4_rects(blrecti_to_blboxi(text_rect_prev), capture_area_border_outline, text_rect_prev_uncovered);
        // XXX: idk if this one is actually worth doing
        BLRectI text_rect_prev_any_buffer_uncovered[4];
        blboxi_get_difference_as_4_rects(blrecti_to_blboxi(text_rect_prev_any_buffer), capture_area_border_outline, text_rect_prev_any_buffer_uncovered);

        for (int i = 0; i < 4; ++i) {
            bl_context_fill_rect_i(
                &st_buffer->bl_ctx,
                &text_rect_prev_uncovered[i]
            );
            // See draw_and_damage_region() comment for why we damage a different
            // region than we blit.
            wl_surface_damage_buffer(
                selection_surface->surface.wl_surface,
                text_rect_prev_any_buffer_uncovered[i].x,
                text_rect_prev_any_buffer_uncovered[i].y,
                text_rect_prev_any_buffer_uncovered[i].w,
                text_rect_prev_any_buffer_uncovered[i].h
            );
        }

        // XXX TODO: See comment above near bl_context_set_comp_op().
        bl_context_set_comp_op(&st_buffer->bl_ctx, comp_op);
        // Restore fill style
        uint32_t prev_fill_style_rgba32;
        bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);
    }

    struct scran_ui_context *ui_ctx = &selection_surface->ui_ctx;

    // Blit new ui
    BLPointI _origin_new_curr_item = new_geometry->origin;
    for (int i = 0; i < textline.n_items; ++i) {
        struct scran_ui_textline_item *item = &textline.items[i];

        const int width_px  = item->width_px;
        const int height_px = scran_ui_font_height_px(ui_ctx);

        if (width_px != 0) {
            // Allocated BLImage dimensions may be larger than its current contents.
            BLRectI area = {
                .x = 0,
                .y = 0,
                .w = width_px,
                .h = height_px,
            };
            bl_context_blit_image_i(&st_buffer->bl_ctx, &_origin_new_curr_item, &item->bl_img, &area);
            _origin_new_curr_item.x += width_px + textline_item_spacing_px;
        }
    }

    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        new_geometry->origin.x,
        new_geometry->origin.y,
        new_geometry->total_width_px,
        textline.meta->height_px
    );

    *prev_geometry            = *new_geometry;
    *prev_geometry_any_buffer = *new_geometry;
}

static inline void
clamp_textline_geometry(
    struct scran_ui_textline_geometry *geometry,
    int min_px,
    int max_px
) {
    const int max_origin_px = max_px - geometry->total_width_px;

    // Prioritize left bound, if the textline is forced to clip
    if (max_origin_px < min_px) {
        geometry->origin.x = min_px;
        return;
    }

    if (geometry->origin.x < min_px) {
        geometry->origin.x = min_px;
    } else if (geometry->origin.x > max_origin_px) {
        geometry->origin.x = max_origin_px;
    }
}

static inline int
get_item_spacing_px(struct scran_ui_context *ui_ctx) {
    return round(3 * ui_ctx->font_advance_fixed_width);
}

static inline void
draw_and_damage_ui(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    bool greeting_screen
) {
    struct scran_ui_context *ui_ctx = &selection_surface->ui_ctx;

    {
        enum scran_ui_redrawn_textline_mask mask = scran_ui_redraw_elements(ui_ctx);
        for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
            selection_surface->double_buffer[i].redrawn_textline_mask |= mask;
        }
    }
    const int item_spacing_px = get_item_spacing_px(ui_ctx);
    const int left_bound      = MAX(0, capture_area_border_outline.x0);

    // Draw below-selection keymap
    {
        struct scran_ui_textline_geometry new_keymap_geometry = {
            .origin = {
                .x = capture_area_border_outline.x0,
                .y = capture_area_border_outline.y1,
            },
            .total_width_px = get_total_textline_width_px(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), item_spacing_px),
        };
        clamp_textline_geometry(&new_keymap_geometry, left_bound, selection_surface->surface.width_px_buffer);
        draw_and_damage_ui_textline(
            selection_surface,
            st_buffer,
            capture_area_border_outline,
            SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap),
            item_spacing_px,
            &st_buffer->ui_keymap_geometry_currently_drawn,
            &selection_surface->ui_keymap_geometry_last_drawn,
            &new_keymap_geometry,
            st_buffer->redrawn_textline_mask & SCRAN_UI_REDREW_KEYMAP
        );
        st_buffer->redrawn_textline_mask &= ~SCRAN_UI_REDREW_KEYMAP;
    }


    // Draw above-selection statusline
    {
        int statusline_total_width_px = get_total_textline_width_px(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline),        item_spacing_px);

        struct scran_ui_textline_geometry new_statusline_geometry = {
            .origin = {
                .x = capture_area_border_outline.x1 - statusline_total_width_px,
                .y = capture_area_border_outline.y0 - scran_ui_font_height_px(ui_ctx),
            },
            .total_width_px = statusline_total_width_px,
        };
        clamp_textline_geometry(&new_statusline_geometry, left_bound, selection_surface->surface.width_px_buffer);
        draw_and_damage_ui_textline(
            selection_surface,
            st_buffer,
            capture_area_border_outline,
            SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline),
            item_spacing_px,
            &st_buffer->ui_statusline_geometry_currently_drawn,
            &selection_surface->ui_statusline_geometry_last_drawn,
            &new_statusline_geometry,
            st_buffer->redrawn_textline_mask & SCRAN_UI_REDREW_STATUSLINE
        );
        st_buffer->redrawn_textline_mask &= ~SCRAN_UI_REDREW_STATUSLINE;
    }


    // Draw greeting
    //   XXX: Must currently be at the end to play nice with scale updates.
    //   TODO: unlikely() ?
    if (greeting_screen) {
        struct scran_ui_textline_geometry new_greeting_geometry = {
            .origin = {
                .x = capture_area_border_outline.x0,
                .y = capture_area_border_outline.y0 - 2 * scran_ui_font_height_px(ui_ctx),
            },
            .total_width_px = get_total_textline_width_px(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_greeting), item_spacing_px),
        };
        clamp_textline_geometry(&new_greeting_geometry, left_bound, selection_surface->surface.width_px_buffer);
        draw_and_damage_ui_textline(
            selection_surface,
            st_buffer,
            capture_area_border_outline,
            SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_greeting),
            item_spacing_px,
            // We have these for greeting as well, despite it not moving,
            // so that it updates correctly on scale changes.
            &st_buffer->ui_greeting_geometry_currently_drawn,
            &selection_surface->ui_greeting_geometry_last_drawn,
            &new_greeting_geometry,
            st_buffer->redrawn_textline_mask & SCRAN_UI_REDREW_GREETING
        );
        st_buffer->redrawn_textline_mask &= ~SCRAN_UI_REDREW_GREETING;
    }
}

// We trunc/ceil like this to make sure that fractionally scaled displays
// will not be able to bleed our capture border into the captured frame,
// not matter how they do their rounding/down-/upscaling.
// This does make our frame not always pixel-perfect with fractional scaling,
// but should not affect non-scaled displays.
static inline BLBoxI
get_scalesafe_border_inline(
    BLBoxI border_inline,
    double normalized_scale_factor
) {
    return (BLBoxI) {
        .x0 = trunc(trunc(border_inline.x0 / normalized_scale_factor) * normalized_scale_factor),
        .y0 = trunc(trunc(border_inline.y0 / normalized_scale_factor) * normalized_scale_factor),
        .x1 = ceil( ceil( border_inline.x1 / normalized_scale_factor) * normalized_scale_factor),
        .y1 = ceil( ceil( border_inline.y1 / normalized_scale_factor) * normalized_scale_factor),
    };
}

static inline BLBoxI
get_border_outline_from_inline(BLBoxI border_inline) {
    return blboxi_get_inflated(border_inline, SCRAN_SELECTION_BORDER_THICKNESS_PX);
}

void
draw_selection_and_damage_buffer(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    struct scran_output_selectionContext *selection_ctx,
    struct BLBoxI capture_area
) {
    if (g_state.options.hide_ui_level >= SCRAN_OPT_HIDE_UI_EVERYTHING) {
        // XXX: Slightly spaghetti, but required for updating the capture area.
        st_buffer->box_currently_drawn = capture_area;
        return;
    }

    // TODO: Assert bl_ctx has already begun

    // What the compositor has to overwrite:
    const struct BLBoxI capture_area_last_used_in_any_buffer = selection_surface->box_last_drawn;
    // What we have to overwrite:
    const struct BLBoxI capture_area_last_used_in_current_buffer = st_buffer->box_currently_drawn;

    assert(!blboxi_is_inverted(capture_area));
    assert(!blboxi_is_inverted(capture_area_last_used_in_any_buffer));
    // TODO: Assert box_bounds fully surrounds box_to_draw

    const struct BLBoxI capture_area_bounds = {
        0,
        0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer,
    };

    const double scale = selection_surface->surface.final_scale_factor_normalized;

    // TODO: Maybe make this all more readable and not 200 columns wide...

    const BLBoxI capture_area_border_inline                             = get_scalesafe_border_inline(capture_area                            , scale);
    const BLBoxI capture_area_border_inline_last_used_in_any_buffer     = get_scalesafe_border_inline(capture_area_last_used_in_any_buffer    , scale);
    const BLBoxI capture_area_border_inline_last_used_in_current_buffer = get_scalesafe_border_inline(capture_area_last_used_in_current_buffer, scale);

    // XXX: Remake the "stroke width" macros
    const BLBoxI capture_area_border_outline                             = get_border_outline_from_inline(capture_area_border_inline);
    const BLBoxI capture_area_border_outline_last_used_in_any_buffer     = get_border_outline_from_inline(capture_area_border_inline_last_used_in_any_buffer);
    const BLBoxI capture_area_border_outline_last_used_in_current_buffer = get_border_outline_from_inline(capture_area_border_inline_last_used_in_current_buffer);

    bool greeting_screen = selection_is_none(selection_ctx);
    bool selection_changed =
        !blboxi_are_equal(capture_area, st_buffer->box_currently_drawn)
        || !blboxi_are_equal(capture_area, selection_surface->box_last_drawn);

    // Draw background dim
    if (selection_changed || st_buffer->force_redraw) {
        int n_damage_regions;
        BLRectI damage_regions_wayland[8];
        BLRectI damage_regions_buffer[8];

        // TODO: Just do redraw/damage directly whenever we need to redraw, rather than
        // needing to branch within this function?
        if (st_buffer->force_redraw) {
            const BLRectI damage_region_everything = blboxi_to_blrecti(capture_area_bounds);
            damage_regions_wayland[0] = damage_region_everything;
            damage_regions_buffer[0] = damage_region_everything;
            n_damage_regions = 1;
        } else {
            static const int i_background_diffs = 0;
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_outline                           , damage_regions_wayland + i_background_diffs);
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_outline                           , damage_regions_buffer  + i_background_diffs);
            static const int i_old_border_diffs = 4;
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_inline_last_used_in_any_buffer    , damage_regions_wayland + i_old_border_diffs);
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_inline_last_used_in_current_buffer, damage_regions_buffer  + i_old_border_diffs);
            n_damage_regions = 8;
        }

        draw_and_damage_background(selection_surface, st_buffer, capture_area_bounds, capture_area_border_outline, damage_regions_wayland, damage_regions_buffer, n_damage_regions, greeting_screen);
    }

    if (g_state.options.hide_ui_level < SCRAN_OPT_HIDE_UI_ITEMS) {
        // Draw keymap
        //   Must be drawn after/on top of background
        draw_and_damage_ui(selection_surface, st_buffer, capture_area_border_outline, greeting_screen);
    }

    // Draw selection border
    if (selection_changed || st_buffer->force_redraw) {
        if (greeting_screen) { // TODO: unlikely()
            st_buffer->box_currently_drawn = capture_area;
        } else {
            BLRectI damage_regions[4];
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions);
            draw_and_damage_selection_border(selection_surface, st_buffer, capture_area, capture_area_border_outline, capture_area_border_inline, damage_regions, damage_regions, 4);
        }
    }


    // NOTE: Don't reset the BLContext here, unless intending to fully
    // re-initialize it. Its state is initialized outside of this ::frame
    // event loop. Shouldn't need flushing either unless doing async.
    bl_context_flush(&st_buffer->bl_ctx, BL_CONTEXT_FLUSH_NO_FLAGS);

    // Assumes force-redraw logic was fully handled above
    st_buffer->force_redraw = false;
}

static inline void
arm_selection_surface_frame_callback(
    struct scran_output *st_output,
    bool commit_if_armed // used as template specialization (if passing a literal)
) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    if (!selection_surface->awaiting_frame_callback && !selection_surface->disable_reason_mask) {
        wl_callback_add_listener(
            wl_surface_frame(selection_surface->surface.wl_surface),
            &selection_surface_frame_callback_listener,
            st_output
        );
        selection_surface->awaiting_frame_callback = true;

        if (commit_if_armed) {
            wl_surface_commit(selection_surface->surface.wl_surface);
        }
    }
}

void
request_selection_surface_frame_callback(
    struct scran_output *st_output
) {
    arm_selection_surface_frame_callback(st_output, true);
}


// Draws the initial state of the buffers and commits.
//
// Caller is responsible for making sure buffer, surface etc. is valid (e.g.
// not busy).
//
// st_output.selection_surface.initial_box initialization must also happen
// prior to calling this function.
void
init_selection_surface_content(
    struct scran_output *st_output
) {
    DEBUG("  init_selection_surface_content()\n");

    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct BLBoxI                         initial_box       =  st_output->initial_selection;

    const bool no_initial_selection = blboxi_are_equal(initial_box, SCRAN_INITIAL_SELECTION_NONE);

    if (no_initial_selection) {
        initial_box = get_selection_surface_pre_selection_box(st_output);
        selection_set_box_px(&st_output->selection_ctx, initial_box);

        // Set fullscreen selection size.
        // XXX TODO: Make this responsibility less disjointed
        scran_ui_statusline_set_selection_size(
            &selection_surface->ui_ctx.ui_statusline,
            blboxi_to_blrecti(get_fullscreen_selection_box(st_output))
        );

        selection_surface_set_theme(st_output, SURFACE_THEME_PRE_SELECTION);
    } else {
        // This must be set prior to set_selection_initialized()
        selection_set_box_px(&st_output->selection_ctx, initial_box);
        // These are usually called at "runtime"/main-loop-time, so call these
        // AFTER init_postmem__selection(), to ensure all relevant runtime
        // state has been set up.
        // ALSO make sure it's called somewhere that the freezeframe init path
        // (and potential future alternate init paths) will reach.
        scran_ui_statusline_set_selection_size(&selection_surface->ui_ctx.ui_statusline, blboxi_to_blrecti(initial_box));
        selection_surface_set_theme(st_output, SURFACE_THEME_DEFAULT);
        selection_set_initialized(st_output);
    }

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &selection_surface->double_buffer[i];
        // Initialized as busy; reset them now.
        //   See init_premem__selection() for more info
        assert(st_buffer->scran_wl_buffer.busy == true);
        st_buffer->scran_wl_buffer.busy = false;
        draw_selection_and_damage_buffer(
            selection_surface,
            st_buffer,
            &st_output->selection_ctx,
            initial_box
        );
    }

    struct scran_output_selectionSurface_buffer *initial_buffer = &selection_surface->double_buffer[0];
    initial_buffer->scran_wl_buffer.busy = true;
    wl_surface_attach(
        selection_surface->surface.wl_surface, initial_buffer->scran_wl_buffer.wl_buffer, 0, 0
    );
    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer
    );

    arm_selection_surface_frame_callback(st_output, false);
    wl_surface_commit(selection_surface->surface.wl_surface);
}
