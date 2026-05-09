#include <math.h>

#include <blend2d/blend2d.h>

#include "state.h"
#include "surface__selection.h"
#include "init.h"
#include "util/blend2d.h"
#include "event-handlers.h"
#include "ui.h"


#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)


// Operation: a - b
static inline void
_get_box_diff_as_4_rects(
    struct BLBoxI a,
    struct BLBoxI b,
    struct BLRectI ret[static 4]
) {
    assert(!SCRAN_BL_BOX_IS_INVERTED(a));
    assert(!SCRAN_BL_BOX_IS_INVERTED(b));

    const struct BLBoxI intersection = {
        .x0 = MAX(a.x0, b.x0),
        .x1 = MIN(a.x1, b.x1),
        .y0 = MAX(a.y0, b.y0),
        .y1 = MIN(a.y1, b.y1),
    };


    if (intersection.x0 > a.x1 || intersection.x1 < a.x0
     || intersection.y0 > a.y1 || intersection.y1 < a.y0
    ) {
        // No overlap
        ret[0] = blboxi_to_blrecti(a);
        ret[1] = (struct BLRectI){ 0 };
        ret[2] = (struct BLRectI){ 0 };
        ret[3] = (struct BLRectI){ 0 };
        return;
    }

    const BLRectI left_full = (struct BLRectI) {
        .x = a.x0,
        .w = intersection.x0 - a.x0,
        .y = a.y0,
        .h = a.y1 - a.y0,
    };

    ret[0] = left_full;

    const BLRectI right_full = (struct BLRectI) {
        .x = intersection.x1,
        .w = a.x1 - intersection.x1,
        .y = a.y0,
        .h = a.y1 - a.y0,
    };

    ret[1] = right_full;

    const BLRectI top_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = a.y0,
        .h = intersection.y0 - a.y0,
    };

    ret[2] = top_remaining;

    const BLRectI bottom_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = intersection.y1,
        .h = a.y1 - intersection.y1,
    };

    ret[3] = bottom_remaining;
}

// Operation: a ^ b
static inline void
_get_box_symdiff_as_4_rects(
    struct BLBoxI a,
    struct BLBoxI b,
    struct BLRectI ret[static 4]
) {
    assert(!SCRAN_BL_BOX_IS_INVERTED(a));
    assert(!SCRAN_BL_BOX_IS_INVERTED(b));

    const struct BLBoxI intersection = {
        .x0 = MAX(a.x0, b.x0),
        .x1 = MIN(a.x1, b.x1),
        .y0 = MAX(a.y0, b.y0),
        .y1 = MIN(a.y1, b.y1),
    };

    const BLBoxI leftmost  = a.x0 < b.x0 ? a : b;
    const BLRectI left_full = (struct BLRectI) {
        .x = leftmost.x0,
        .w = intersection.x0 - leftmost.x0,
        .y = leftmost.y0,
        .h = leftmost.y1 - leftmost.y0,
    };

    ret[0] = left_full;

    const BLBoxI rightmost = a.x1 > b.x1 ? a : b;
    const BLRectI right_full = (struct BLRectI) {
        .x = intersection.x1,
        .w = rightmost.x1 - intersection.x1,
        .y = rightmost.y0,
        .h = rightmost.y1 - rightmost.y0,
    };

    ret[1] = right_full;

    const BLRectI top_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = MIN(a.y0, b.y0),
        .h = intersection.y0 - MIN(a.y0, b.y0),
    };

    ret[2] = top_remaining;

    const BLRectI bottom_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = intersection.y1,
        .h = MAX(a.y1, b.y1) - intersection.y1,
    };

    ret[3] = bottom_remaining;
}

static inline void
_draw_and_damage_region(
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
_draw_and_damage_selection_border(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    BLBoxI capture_area_border_inline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_inline,  BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);

    for (int i = 0; i < n_damage_regions; ++i) {
        _draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    bl_path_clear(&selection_surface->bl_path);
}

static inline void
_draw_and_damage_background(
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
        _draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    uint32_t prev_fill_style_rgba32;
    bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);

    bl_path_clear(&selection_surface->bl_path);
}


static inline int
_get_total_keymap_width_px(
    struct scran_ui_keymap *keymap,
    int item_spacing_px
) {
    int total_width_px = 0;

    for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        struct scran_ui_keymap_item *keymap_item = &keymap->items[i];
        if (keymap_item->width_px != 0) {
            total_width_px += keymap_item->width_px + item_spacing_px;
        }
    }

    // Switch to if statement if no-text scenarios will be possible in the future.
    assert(total_width_px != 0);
    total_width_px -= item_spacing_px;

    return total_width_px;
}

static inline void
_draw_and_damage_keymap(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline
) {
    struct scran_ui_context *ui_ctx = &selection_surface->ui_ctx;
    struct scran_ui_keymap  *keymap = &selection_surface->ui_ctx.ui_keymap;

    bool ui_was_dirty = ui_ctx->dirty;

    if (ui_was_dirty) {
        redraw_keymap(ui_ctx);
        ui_ctx->dirty = false;
    }

    const int item_spacing_px = 3 * ui_ctx->fixed_width_font_glyph_width_px;
    const int total_width_px = _get_total_keymap_width_px(keymap, item_spacing_px);

    struct scran_ui_keymap_surface_state *state_prev            = &st_buffer->ui_keymap_state_currently_drawn;
    struct scran_ui_keymap_surface_state *state_prev_any_buffer = &selection_surface->ui_keymap_state_last_drawn;
    struct scran_ui_keymap_surface_state  state_new = {
        .origin         = {
            .x = capture_area_border_outline.x0,
            .y = capture_area_border_outline.y1,
        },
        .total_width_px = total_width_px,
    };

    bool should_redraw =
        st_buffer->force_redraw
        || ui_was_dirty
        || !blpointi_are_equal(state_prev->origin, state_new.origin)
        || state_prev->total_width_px != state_new.total_width_px
    ;
    if (!should_redraw) {
        return;
    }

    // Clamp to buffer width
    if (state_new.origin.x < 0) {
        state_new.origin.x = 0;
    } else if ((state_new.origin.x + state_new.total_width_px) > selection_surface->surface.width_px_buffer) {
        state_new.origin.x = selection_surface->surface.width_px_buffer - state_new.total_width_px;
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
            .h = keymap->height_px,
        };

        BLRectI text_rect_prev_any_buffer = {
            .x = state_prev_any_buffer->origin.x,
            .y = state_prev_any_buffer->origin.y,
            .w = state_prev_any_buffer->total_width_px,
            .h = keymap->height_px,
        };

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, SCRAN_SELECTION_BACKGROUND_COLOR.value);

        BLRectI text_rect_prev_uncovered[4];
        _get_box_diff_as_4_rects(blrecti_to_blboxi(text_rect_prev), capture_area_border_outline, text_rect_prev_uncovered);
        // XXX: idk if this one is actually worth doing
        BLRectI text_rect_prev_any_buffer_uncovered[4];
        _get_box_diff_as_4_rects(blrecti_to_blboxi(text_rect_prev_any_buffer), capture_area_border_outline, text_rect_prev_any_buffer_uncovered);

        for (int i = 0; i < 4; ++i) {
            bl_context_fill_rect_i(
                &st_buffer->bl_ctx,
                &text_rect_prev_uncovered[i]
            );
            // See _draw_and_damage_region() comment for why we damage a different
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
        bl_context_set_comp_op(&st_buffer->bl_ctx, BL_COMP_OP_SRC_OVER);
        // Restore fill style
        uint32_t prev_fill_style_rgba32;
        bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);
    }

    // Blit new ui
    BLPointI _origin_new_curr_item = state_new.origin;
    for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        struct scran_ui_keymap_item *keymap_item = &keymap->items[i];

        const int width_px  = keymap_item->width_px;
        const int height_px = ui_ctx->font_height_px;

        if (width_px != 0) {
            // Allocated BLImage dimensions may be larger than its current contents.
            BLRectI area = {
                .x = 0,
                .y = 0,
                .w = width_px,
                .h = height_px,
            };
            bl_context_blit_image_i(&st_buffer->bl_ctx, &_origin_new_curr_item, &keymap_item->bl_img, &area);
            _origin_new_curr_item.x += width_px + item_spacing_px;
        }
    }

    // See _get_total_keymap_width_px()
    assert(_origin_new_curr_item.x != 0);
    assert((_origin_new_curr_item.x - state_new.origin.x) - item_spacing_px == total_width_px);

    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        state_new.origin.x,
        state_new.origin.y,
        state_new.total_width_px,
        keymap->height_px
    );

    selection_surface->ui_keymap_state_last_drawn = state_new;
    st_buffer->ui_keymap_state_currently_drawn    = state_new;
}

// We trunc/ceil like this to make sure that fractionally scaled displays
// will not be able to bleed our capture border into the captured frame,
// not matter how they do their rounding/down-/upscaling.
// This does make our frame not always pixel-perfect with fractional scaling,
// but should not affect non-scaled displays.
static inline BLBoxI
_get_scalesafe_border_inline(
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

    assert(!SCRAN_BL_BOX_IS_INVERTED(capture_area));
    assert(!SCRAN_BL_BOX_IS_INVERTED(capture_area_last_used_in_any_buffer));
    // TODO: Assert box_bounds fully surrounds box_to_draw

    const struct BLBoxI capture_area_bounds = {
        0,
        0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer,
    };

    const double scale = selection_surface->surface.final_scale_factor_normalized;

    // TODO: Maybe make this all more readable and not 200 columns wide...

    const BLBoxI capture_area_border_inline                             = _get_scalesafe_border_inline(capture_area                            , scale);
    const BLBoxI capture_area_border_inline_last_used_in_any_buffer     = _get_scalesafe_border_inline(capture_area_last_used_in_any_buffer    , scale);
    const BLBoxI capture_area_border_inline_last_used_in_current_buffer = _get_scalesafe_border_inline(capture_area_last_used_in_current_buffer, scale);

    // XXX: Remake the "stroke width" macros
    const BLBoxI capture_area_border_outline                             = get_blboxi_inflated(capture_area_border_inline                            , SCRAN_SELECTION_BORDER_THICKNESS_PX);
    const BLBoxI capture_area_border_outline_last_used_in_any_buffer     = get_blboxi_inflated(capture_area_border_inline_last_used_in_any_buffer    , SCRAN_SELECTION_BORDER_THICKNESS_PX);
    const BLBoxI capture_area_border_outline_last_used_in_current_buffer = get_blboxi_inflated(capture_area_border_inline_last_used_in_current_buffer, SCRAN_SELECTION_BORDER_THICKNESS_PX);

    // TODO: Just do redraw/damage directly whenever we need to redraw, rather
    // than doing it here with a flag.
    if (st_buffer->force_redraw) { // TODO: unlikely()
        // Draw background dim
        const BLRectI damage_region_everything = blboxi_to_blrecti(capture_area_bounds);
        _draw_and_damage_background(      selection_surface, st_buffer, capture_area_bounds        , capture_area_border_outline, &damage_region_everything,       &damage_region_everything,       1);

        // Draw keymap
        _draw_and_damage_keymap(selection_surface, st_buffer, capture_area_border_outline);

        // Draw selection border
        BLRectI damage_regions_selection_border[4];
        _get_box_symdiff_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions_selection_border);
        _draw_and_damage_selection_border(selection_surface, st_buffer, capture_area_border_outline, capture_area_border_inline , damage_regions_selection_border, damage_regions_selection_border, 4);

        st_buffer->force_redraw = false;
    } else {
        // Draw background dim
        {
            BLRectI damage_regions_wayland[8];
            BLRectI damage_regions_buffer[8];

            static const int i_background_diffs = 0;
            _get_box_symdiff_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_outline                           , damage_regions_wayland + i_background_diffs);
            _get_box_symdiff_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_outline                           , damage_regions_buffer  + i_background_diffs);

            static const int i_old_border_diffs = 4;
            _get_box_symdiff_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_inline_last_used_in_any_buffer    , damage_regions_wayland + i_old_border_diffs);
            _get_box_symdiff_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_inline_last_used_in_current_buffer, damage_regions_buffer  + i_old_border_diffs);

            _draw_and_damage_background(selection_surface, st_buffer, capture_area_bounds, capture_area_border_outline, damage_regions_wayland, damage_regions_buffer, 8);

            // Draw keymap
            _draw_and_damage_keymap(selection_surface, st_buffer, capture_area_border_outline);
        }

        // Draw selection border
        {
            BLRectI damage_regions[4];

            _get_box_symdiff_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions);

            _draw_and_damage_selection_border(selection_surface, st_buffer, capture_area_border_outline, capture_area_border_inline, damage_regions, damage_regions, 4);
        }
    }

    // NOTE: Don't reset the BLContext here, unless intending to fully
    // re-initialize it. Its state is initialized outside of this ::frame
    // event loop. Shouldn't need flushing either unless doing async.
    bl_context_flush(&st_buffer->bl_ctx, BL_CONTEXT_FLUSH_NO_FLAGS);
}

static inline void
_arm_selection_surface_frame_callback(
    struct scran_output *st_output,
    bool commit_if_armed // used as template specialization (if passing a literal)
) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    if (selection_surface->awaiting_frame_callback == false) {
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
    _arm_selection_surface_frame_callback(st_output, true);
}

// Caller is responsible for making sure buffer is valid (e.g. not busy)
void
force_update_selection_surface(
    struct scran_output *st_output,
    struct scran_output_selectionSurface_buffer *st_buffer,
    struct BLBoxI box
) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    assert(st_buffer->busy == false);
    st_buffer->busy = true;
    draw_selection_and_damage_buffer(
        selection_surface,
        st_buffer,
        box
    );
    // TODO: Probably move box_currently_drawn setting responsibility into the
    // actual drawing function (draw_selection_and_damage_buffer()).
    st_buffer->box_currently_drawn = box;
    st_output->selection_ctx.box_px = box;
    wl_surface_attach(selection_surface->surface.wl_surface, st_buffer->wl_buffer, 0, 0);

    _arm_selection_surface_frame_callback(st_output, false);
    wl_surface_commit(selection_surface->surface.wl_surface);
}

