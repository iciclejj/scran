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
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    // TODO: Just store the fill styles in state
    BLVarCore prev_fill_style = { };
    bl_context_get_fill_style(&st_buffer->bl_ctx, &prev_fill_style);

    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, SCRAN_SELECTION_BACKGROUND_COLOR.value);

    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_max_bounds,     BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);

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
    struct scran_ui_textline *textline,
    int n_textline_items,
    int item_spacing_px
) {
    int total_width_px = 0;

    for (int i = 0; i < n_textline_items; ++i) {
        struct scran_ui_textline_item *item = &textline->items[i];
        if (item->width_px != 0) {
            total_width_px += item->width_px + item_spacing_px;
        }
    }

    total_width_px -= (total_width_px == 0) ? 0 : item_spacing_px;

    return total_width_px;
}


static inline bool
textline_surface_state_changed(
    struct scran_ui_textline_surface_state *prev,
    struct scran_ui_textline_surface_state *new
) {
    return !blpointi_are_equal(prev->origin, new->origin)
        || prev->total_width_px != new->total_width_px;
}

static inline void
draw_and_damage_ui_textline(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    struct scran_ui_textline *textline,
    // XXX: Maybe just put this into the textline struct, even if it's a static value?
    int n_textline_items,
    int textline_item_spacing_px,
    struct scran_ui_textline_surface_state *state_prev,
    struct scran_ui_textline_surface_state *state_prev_any_buffer,
    struct scran_ui_textline_surface_state *state_new,
    bool force_redraw
) {
    if ( !(force_redraw || textline_surface_state_changed(state_prev, state_new))) {
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
            .x = state_prev->origin.x,
            .y = state_prev->origin.y,
            .w = state_prev->total_width_px,
            .h = textline->meta.height_px,
        };

        BLRectI text_rect_prev_any_buffer = {
            .x = state_prev_any_buffer->origin.x,
            .y = state_prev_any_buffer->origin.y,
            .w = state_prev_any_buffer->total_width_px,
            .h = textline->meta.height_px,
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
    BLPointI _origin_new_curr_item = state_new->origin;
    for (int i = 0; i < n_textline_items; ++i) {
        struct scran_ui_textline_item *item = &textline->items[i];

        const int width_px  = item->width_px;
        const int height_px = ui_ctx->font_height_px;

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
        state_new->origin.x,
        state_new->origin.y,
        state_new->total_width_px,
        textline->meta.height_px
    );

    *state_prev            = *state_new;
    *state_prev_any_buffer = *state_new;
}


static inline void
clamp_textline_surface_state(
    struct scran_ui_textline_surface_state *state_new,
    int min_px,
    int max_px
) {
    // Clamp to buffer width
    if (state_new->origin.x < min_px) {
        state_new->origin.x = min_px;
    } else if ((state_new->origin.x + state_new->total_width_px) > max_px) {
        state_new->origin.x = max_px - state_new->total_width_px;
    }
}

static inline void
draw_and_damage_ui(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline
) {
    struct scran_ui_context  *ui_ctx = &selection_surface->ui_ctx;

    bool ui_was_dirty = ui_ctx->dirty;

    if (ui_was_dirty) {
        scran_ui_redraw_elements(ui_ctx);
        assert(ui_ctx->dirty == false);
    }

    const bool force_redraw    = ui_was_dirty || st_buffer->force_redraw;
    const int  item_spacing_px = 3 * ui_ctx->fixed_width_font_glyph_width_px;

    // Draw below-selection keymap
    {
        struct scran_ui_textline_surface_state state_new_keymap = {
            .origin = {
                .x = capture_area_border_outline.x0,
                .y = capture_area_border_outline.y1,
            },
            .total_width_px = get_total_textline_width_px(&ui_ctx->ui_keymap, SCRAN_UI_KEYMAP_N_ITEMS, item_spacing_px),
        };
        clamp_textline_surface_state(&state_new_keymap, 0, selection_surface->surface.width_px_buffer);
        draw_and_damage_ui_textline(
            selection_surface,
            st_buffer,
            capture_area_border_outline,
            &ui_ctx->ui_keymap,
            SCRAN_UI_KEYMAP_N_ITEMS,
            item_spacing_px,
            &st_buffer->ui_keymap_state_currently_drawn,
            &selection_surface->ui_keymap_state_last_drawn,
            &state_new_keymap,
            force_redraw
        );
    }


    // Draw above-selection statusline-keymap & statusline
    {
        int statusline_total_width_px        = get_total_textline_width_px(&ui_ctx->ui_statusline,        SCRAN_UI_STATUSLINE_N_ITEMS,        item_spacing_px);
        int statusline_keymap_total_width_px = get_total_textline_width_px(&ui_ctx->ui_statusline_keymap, SCRAN_UI_STATUSLINE_KEYMAP_N_ITEMS, item_spacing_px);

        struct scran_ui_textline_surface_state state_new_statusline = {
            .origin = {
                .x = capture_area_border_outline.x1 - statusline_total_width_px,
                .y = capture_area_border_outline.y0 - ui_ctx->font_height_px,
            },
            .total_width_px = statusline_total_width_px,
        };
        struct scran_ui_textline_surface_state state_new_statusline_keymap = {
            .origin = {
                .x = capture_area_border_outline.x0,
                .y = capture_area_border_outline.y0 - ui_ctx->font_height_px,
            },
            .total_width_px = statusline_keymap_total_width_px,
        };

        // The left-aligned `statusline_keymap`s anchor has priority, but it should still
        // make space for the right-aligned `statusline` when reaching the far-right of
        // the screen.
        // Also add enough spacing between them so *at least their logical boxes* don't
        // overlap and start clearing each other out.
        int ui_element_spacing_px = (statusline_total_width_px == 0 || statusline_keymap_total_width_px == 0) ? 0 : item_spacing_px;
        clamp_textline_surface_state(
            &state_new_statusline_keymap,
            0,
            // Don't overlap the right-aligned statusline, which is clamped to buffer width
            selection_surface->surface.width_px_buffer - (statusline_total_width_px + ui_element_spacing_px)
        );
        clamp_textline_surface_state(
            &state_new_statusline,
            // Don't overlap the left-aligned statusline_keymap
            state_new_statusline_keymap.origin.x + (statusline_keymap_total_width_px + ui_element_spacing_px),
            selection_surface->surface.width_px_buffer
        );

        draw_and_damage_ui_textline(
            selection_surface,
            st_buffer,
            capture_area_border_outline,
            &ui_ctx->ui_statusline_keymap,
            SCRAN_UI_STATUSLINE_KEYMAP_N_ITEMS,
            item_spacing_px,
            &st_buffer->ui_statusline_keymap_state_currently_drawn,
            &selection_surface->ui_statusline_keymap_state_last_drawn,
            &state_new_statusline_keymap,
            force_redraw
        );
        draw_and_damage_ui_textline(
            selection_surface,
            st_buffer,
            capture_area_border_outline,
            &ui_ctx->ui_statusline,
            SCRAN_UI_STATUSLINE_N_ITEMS,
            item_spacing_px,
            &st_buffer->ui_statusline_state_currently_drawn,
            &selection_surface->ui_statusline_state_last_drawn,
            &state_new_statusline,
            force_redraw
        );
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

void
draw_selection_and_damage_buffer(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    struct BLBoxI capture_area
) {
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
    const BLBoxI capture_area_border_outline                             = blboxi_get_inflated(capture_area_border_inline                            , SCRAN_SELECTION_BORDER_THICKNESS_PX);
    const BLBoxI capture_area_border_outline_last_used_in_any_buffer     = blboxi_get_inflated(capture_area_border_inline_last_used_in_any_buffer    , SCRAN_SELECTION_BORDER_THICKNESS_PX);
    const BLBoxI capture_area_border_outline_last_used_in_current_buffer = blboxi_get_inflated(capture_area_border_inline_last_used_in_current_buffer, SCRAN_SELECTION_BORDER_THICKNESS_PX);

    // Draw background dim
    {
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

        draw_and_damage_background(selection_surface, st_buffer, capture_area_bounds, capture_area_border_outline, damage_regions_wayland, damage_regions_buffer, n_damage_regions);
    }

    // Draw keymap
    //   Must be drawn after/on top of background
    draw_and_damage_ui(selection_surface, st_buffer, capture_area_border_outline);

    // Draw selection border
    {
        BLRectI damage_regions[4];
        blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions);
        draw_and_damage_selection_border(selection_surface, st_buffer, capture_area, capture_area_border_outline, capture_area_border_inline, damage_regions, damage_regions, 4);
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

    if (!selection_surface->awaiting_frame_callback && !selection_surface->frame_callbacks_disabled) {
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
        // We want to draw the splash text in the top left corner.
        initial_box = (BLBoxI){0};
        st_output->selection_ctx.box_px = initial_box;
        set_selection_surface_theme(st_output, SURFACE_THEME_PRE_SELECTION);
    } else {
        // This must be set prior to set_selection_initialized()
        st_output->selection_ctx.box_px = initial_box;
        // These are usually called at "runtime"/main-loop-time, so call these
        // AFTER init_postmem__selection(), to ensure all relevant runtime
        // state has been set up.
        // ALSO make sure it's called somewhere that the freezeframe init path
        // (and potential future alternate init paths) will reach.
        scran_ui_set_selection_stage_defaults(&selection_surface->ui_ctx);
        set_selection_surface_theme(st_output, SURFACE_THEME_DEFAULT);
        set_selection_initialized(st_output);
    }

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &selection_surface->double_buffer[i];
        // Initialized as busy; reset them now.
        //   See init_premem__selection() for more info
        assert(st_buffer->busy == true);
        st_buffer->busy = false;
        draw_selection_and_damage_buffer(
            selection_surface,
            st_buffer,
            initial_box
        );
    }

    struct scran_output_selectionSurface_buffer *initial_buffer = &selection_surface->double_buffer[0];
    initial_buffer->busy = true;
    wl_surface_attach(
        selection_surface->surface.wl_surface, initial_buffer->wl_buffer, 0, 0
    );
    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer
    );

    arm_selection_surface_frame_callback(st_output, false);
    set_force_redraw_selection_surface_buffers(st_output);
    wl_surface_commit(selection_surface->surface.wl_surface);
}

