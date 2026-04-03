#include <math.h>

#include <blend2d/blend2d.h>

#include "state.h"
#include "init.h"
#include "util/blend2d.h"


#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)


static inline void
_get_box_diffs_as_4_rects(
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
    struct scran_output_surface *st_surface,
    struct scran_output_surface_buffer *st_buffer,
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
    bl_context_fill_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &st_surface->bl_path);
    bl_context_restore_clipping(&st_buffer->bl_ctx);

    wl_surface_damage_buffer( st_surface->wl_surface,
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
    struct scran_output_surface *st_surface,
    struct scran_output_surface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    BLBoxI capture_area_border_inline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    bl_path_add_box_i(&st_surface->bl_path, &capture_area_border_inline,  BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&st_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);

    for (int i = 0; i < n_damage_regions; ++i) {
        _draw_and_damage_region(st_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    bl_path_reset(&st_surface->bl_path);
}

static inline void
_draw_and_damage_background(
    struct scran_output_surface *st_surface,
    struct scran_output_surface_buffer *st_buffer,
    BLBoxI capture_area_max_bounds,
    BLBoxI capture_area_border_outline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    // TODO: Just store the fill styles in state
    BLVarCore prev_fill_style;
    bl_context_get_fill_style(&st_buffer->bl_ctx, &prev_fill_style);

    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, BLCONTEXT_RGBA32_BACKGROUND_DIM.value);

    bl_path_add_box_i(&st_surface->bl_path, &capture_area_max_bounds,     BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&st_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);

    for (int i = 0; i < n_damage_regions; ++i) {
        _draw_and_damage_region(st_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    uint32_t prev_fill_style_rgba32;
    bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);

    bl_path_reset(&st_surface->bl_path);
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
draw_frame_and_damage_buffer(
    struct scran_output_surface *st_surface,
    struct scran_output_surface_buffer *st_buffer,
    struct BLBoxI capture_area,
    struct BLBoxI capture_area_bounds
) {
    // TODO: Assert bl_ctx has already begun

    // What the compositor has to overwrite:
    const struct BLBoxI capture_area_last_used_in_any_buffer = st_surface->box_last_drawn;
    // What we have to overwrite:
    const struct BLBoxI capture_area_last_used_in_current_buffer = st_buffer->box_currently_drawn;

    assert(!SCRAN_BL_BOX_IS_INVERTED(capture_area));
    assert(!SCRAN_BL_BOX_IS_INVERTED(capture_area_last_used_in_any_buffer));
    // TODO: Assert box_bounds fully surrounds box_to_draw

    const double scale = st_surface->final_scale_factor_normalized;

    // TODO: Maybe make this all more readable and not 200 columns wide...

    const BLBoxI capture_area_border_inline                             = _get_scalesafe_border_inline(capture_area                            , scale);
    const BLBoxI capture_area_border_inline_last_used_in_any_buffer     = _get_scalesafe_border_inline(capture_area_last_used_in_any_buffer    , scale);
    const BLBoxI capture_area_border_inline_last_used_in_current_buffer = _get_scalesafe_border_inline(capture_area_last_used_in_current_buffer, scale);

    // XXX: Remake the "stroke width" macros
    const BLBoxI capture_area_border_outline                             = get_blboxi_inflated(capture_area_border_inline                            , SCRAN_SELECTION_BORDER_THICKNESS_PX);
    const BLBoxI capture_area_border_outline_last_used_in_any_buffer     = get_blboxi_inflated(capture_area_border_inline_last_used_in_any_buffer    , SCRAN_SELECTION_BORDER_THICKNESS_PX);
    const BLBoxI capture_area_border_outline_last_used_in_current_buffer = get_blboxi_inflated(capture_area_border_inline_last_used_in_current_buffer, SCRAN_SELECTION_BORDER_THICKNESS_PX);

    if (st_buffer->force_redraw) { // TODO: unlikely()
        // Draw background dim
        const BLRectI damage_region_everything = blboxi_to_blrecti(capture_area_bounds);
        _draw_and_damage_background(      st_surface, st_buffer, capture_area_bounds        , capture_area_border_outline, &damage_region_everything,       &damage_region_everything,       1);

        // Draw selection border
        BLRectI damage_regions_selection_border[4];
        _get_box_diffs_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions_selection_border);
        _draw_and_damage_selection_border(st_surface, st_buffer, capture_area_border_outline, capture_area_border_inline , damage_regions_selection_border, damage_regions_selection_border, 4);

        st_buffer->force_redraw = false;
    } else {
        // Draw background dim
        {
            BLRectI damage_regions_wayland[8];
            BLRectI damage_regions_buffer[8];

            static const int i_background_diffs = 0;
            _get_box_diffs_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_outline                           , damage_regions_wayland + i_background_diffs);
            _get_box_diffs_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_outline                           , damage_regions_buffer  + i_background_diffs);

            static const int i_old_border_diffs = 4;
            _get_box_diffs_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_inline_last_used_in_any_buffer    , damage_regions_wayland + i_old_border_diffs);
            _get_box_diffs_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_inline_last_used_in_current_buffer, damage_regions_buffer  + i_old_border_diffs);

            _draw_and_damage_background(st_surface, st_buffer, capture_area_bounds, capture_area_border_outline, damage_regions_wayland, damage_regions_buffer, 8);
        }

        // Draw selection border
        {
            BLRectI damage_regions[4];

            _get_box_diffs_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions);

            _draw_and_damage_selection_border(st_surface, st_buffer, capture_area_border_outline, capture_area_border_inline, damage_regions, damage_regions, 4);
        }
    }

    // NOTE: Don't reset the BLContext here, unless intending to fully
    // re-initialize it. Its state is initialized outside of this ::frame
    // event loop. Shouldn't need flushing either unless doing async.
    bl_context_flush(&st_buffer->bl_ctx, BL_CONTEXT_FLUSH_NO_FLAGS);
}

