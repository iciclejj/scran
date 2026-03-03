#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "init.h"
#include "state.h"
#include "event-handlers.h"
#include "util/blend2d.h"

#include "print.h"


#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)


extern struct scran g_state;


struct _box_diffs {
    struct BLBoxI left_full;
    struct BLBoxI right_full;
    // "_remaining", i.e. not including intersection with leftmost/topmost:
    struct BLBoxI top_remaining;
    struct BLBoxI bottom_remaining;
};


// Call wl_surface_damage_buffer on the difference between the areas of two
// BLBoxI boxes (i.e. union minus intersection).
static inline struct _box_diffs
get_box_diffs(struct BLBoxI a, struct BLBoxI b)
{
    assert(!SCRAN_BL_BOX_IS_INVERTED(a));
    assert(!SCRAN_BL_BOX_IS_INVERTED(b));

    const struct BLBoxI intersection = {
        .x0 = MAX(a.x0, b.x0),
        .x1 = MIN(a.x1, b.x1),
        .y0 = MAX(a.y0, b.y0),
        .y1 = MIN(a.y1, b.y1),
    };

    struct _box_diffs diff;

    const struct BLBoxI leftmost = a.x0 < b.x0 ? a : b;
    diff.left_full = (struct BLBoxI) {
        .x0 = leftmost.x0,
        .x1 = intersection.x0,
        .y0 = leftmost.y0,
        .y1 = leftmost.y1,
    };

    const struct BLBoxI rightmost = a.x1 > b.x1 ? a : b;
    diff.right_full = (struct BLBoxI) {
        .x0 = intersection.x1,
        .x1 = rightmost.x1,
        .y0 = rightmost.y0,
        .y1 = rightmost.y1,
    };

    diff.top_remaining = (struct BLBoxI) {
        .x0 = intersection.x0,
        .x1 = intersection.x1,
        .y0 = MIN(a.y0, b.y0),
        .y1 = intersection.y1,
    };

    diff.bottom_remaining = (struct BLBoxI) {
        .x0 = intersection.x0,
        .x1 = intersection.x1,
        .y0 = intersection.y1,
        .y1 = MAX(a.y1, b.y1),
    };

    return diff;
}


static inline struct scran_output_surface_buffer *
get_free_double_buffer(struct scran_output *st_output)
{
    struct scran_output_surface_buffer *buffer =
        st_output->surface.double_buffer[0].busy
        ? &st_output->surface.double_buffer[1]
        : &st_output->surface.double_buffer[0]
    ;

    if (buffer->busy) {
        return NULL;
    }

    return buffer;
}


// TODO: Move into blend2d utils header
static inline bool
_boxes_are_equal(BLBoxI a, BLBoxI b)
{
    return  a.x0 == b.x0 &&
            a.x1 == b.x1 &&
            a.y0 == b.y0 &&
            a.y1 == b.y1
    ;
}


static inline void
_draw_and_damage_region(
    struct scran_output_surface *st_surface,
    struct scran_output_surface_buffer *st_buffer,
    BLRectI damage_region
) {
    // TODO: Is bl_context_clear_all the same as bl_context_clear_rect_i, if we do it after clipping?
    bl_context_clip_to_rect_i(&st_buffer->bl_ctx, &damage_region);
    bl_context_clear_all(&st_buffer->bl_ctx);
    bl_context_fill_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &st_surface->bl_path);
    bl_context_restore_clipping(&st_buffer->bl_ctx);
    wl_surface_damage_buffer( st_surface->wl_surface,
        damage_region.x, damage_region.y, damage_region.w, damage_region.h
    );
}


static inline void
draw_frame_and_damage_buffer(
    struct scran_output_surface *st_surface,
    struct scran_output_surface_buffer *st_buffer,
    struct BLBoxI box_to_draw,
    struct BLBoxI box_bounds
) {
    const struct BLBoxI box_already_drawn = st_surface->bl_box_currently_drawn;

    // TODO: Assert box_bounds fully surrounds box_to_draw
    assert(!SCRAN_BL_BOX_IS_INVERTED(box_to_draw));
    assert(!SCRAN_BL_BOX_IS_INVERTED(box_already_drawn));
    // Equal boxes should have been skipped.
    assert(!_boxes_are_equal(box_to_draw, box_already_drawn));

    bl_path_add_box_i(&st_surface->bl_path, &box_bounds, BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&st_surface->bl_path, &box_to_draw, BL_GEOMETRY_DIRECTION_NONE);

    const struct _box_diffs box_diffs = get_box_diffs(box_to_draw, box_already_drawn);

    // TODO: Just make get_box_diffs return rects, probably...
    _draw_and_damage_region(st_surface, st_buffer, blboxi_to_blrecti(box_diffs.left_full));
    _draw_and_damage_region(st_surface, st_buffer, blboxi_to_blrecti(box_diffs.right_full));
    _draw_and_damage_region(st_surface, st_buffer, blboxi_to_blrecti(box_diffs.top_remaining));
    _draw_and_damage_region(st_surface, st_buffer, blboxi_to_blrecti(box_diffs.bottom_remaining));
    st_surface->bl_box_currently_drawn = box_to_draw;

    // NOTE: Don't reset the BLContext here, unless intending to fully
    // re-initialize it. Its state is initialized outside of this ::frame
    // event loop. Shouldn't need flushing either unless doing async.
    bl_path_reset(&st_surface->bl_path);
    bl_context_flush(&st_buffer->bl_ctx, BL_CONTEXT_FLUSH_NO_FLAGS);
}


// TODO: Look at this again to see whether it handles inverted box. If not,
// then assert not inverted
static inline struct BLBoxI
_get_reverse_transform(
    struct BLBoxI box,
    uint32_t source_width,
    uint32_t source_height,
    enum wl_output_transform transform
) {
    uint32_t tmp, tmp2;

// TODO: -1 to turn length into index?
#define _flip_horizontally() \
        box.x0 = source_width - box.x1; \
        box.x1 = source_width - box.x0;

    switch (transform) {
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        _flip_horizontally();
    case WL_OUTPUT_TRANSFORM_NORMAL:
        return box;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        _flip_horizontally();
    case WL_OUTPUT_TRANSFORM_90:
        tmp = box.x0;
        box.x0 = box.y0;
        box.y0 = source_height - box.x1;
        box.x1 = box.y1;
        box.y1 = source_height - tmp/*x0*/;
        return box;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        _flip_horizontally();
    case WL_OUTPUT_TRANSFORM_180:
        tmp = box.y0;
        box.y0 = source_height - box.y1;
        box.y1 = source_height - tmp;
        tmp = box.x0;
        box.x0 = source_width - box.x1;
        box.x1 = source_width - tmp;
        return box;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        _flip_horizontally();
    case WL_OUTPUT_TRANSFORM_270:
        tmp = box.x0;
        tmp2 = box.x1;
        box.x0 = source_width - box.y1;
        box.x1 = source_width - box.y0;
        box.y0 = tmp;
        box.y1 = tmp2;
        return box;
    }

#undef _flip_horizontally
}


static void
surface_frame_callback_handler(
    void *data,
    struct wl_callback *callback,
    uint32_t time_ms
) {
    // Destroy callback here and request new frame "recursively" within callback
    wl_callback_destroy(callback);

    struct scran_output *st_output = data;

    if (g_state.exit_requested) {
        // Quit before requesting another frame
        return;
    }

    struct scran_output_surface_buffer *st_buffer = get_free_double_buffer(st_output);

    if (st_buffer == NULL) {
        DEBUG("Both buffers busy...\n");
        goto go_next;
    }

    // TODO: Also assert it's clamped?
    const struct BLBoxI normalized_box_to_draw = get_blboxi_deinverted(st_output->selection_ctx.bl_box);
    const struct BLBoxI box_currently_drawn = st_output->surface.bl_box_currently_drawn;

    if (_boxes_are_equal(normalized_box_to_draw, box_currently_drawn)) {
        goto go_next;
    }

    // NOTE: Must be set here to sync with selection box rendering.
    //       Otherwise, rendered selection can lag behind the capture area,
    //        leading to f.ex. capture frame border spilling into the actual
    //        capture frame
    //       See also comment in scran_capture.
    st_output->capture.frame_ctx.capture_area_px = _get_reverse_transform(
        normalized_box_to_draw,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );

    st_buffer->busy = true;
    draw_frame_and_damage_buffer(
        &st_output->surface,
        st_buffer,
        normalized_box_to_draw,
        st_output->selection_ctx.bl_box_bounds
    );
    wl_surface_attach(st_output->surface.wl_surface, st_buffer->wl_buffer, 0, 0);
go_next:
    wl_callback_add_listener(
        wl_surface_frame(st_output->surface.wl_surface),
        &surface_frame_callback_listener,
        st_output
    );
    wl_surface_commit(st_output->surface.wl_surface);
}


struct wl_callback_listener surface_frame_callback_listener = {
    .done = surface_frame_callback_handler
};

