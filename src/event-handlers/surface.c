#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "presentation-time.h"

#include "init.h"
#include "state.h"
#include "event-handlers.h"
#include "util/blend2d.h"

#include "print.h"


#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)


extern struct scran g_state;


struct _rect_diffs {
    struct BLRectI left_full;
    struct BLRectI right_full;
    // "_remaining", i.e. not including intersection with leftmost/topmost:
    struct BLRectI top_remaining;
    struct BLRectI bottom_remaining;
};

// Call wl_surface_damage_buffer on the difference between the areas of two
// BLBoxI boxes (i.e. union minus intersection).
static inline struct _rect_diffs
get_box_diffs_as_rects(struct BLBoxI a, struct BLBoxI b)
{
    assert(!SCRAN_BL_BOX_IS_INVERTED(a));
    assert(!SCRAN_BL_BOX_IS_INVERTED(b));

    const struct BLBoxI intersection = {
        .x0 = MAX(a.x0, b.x0),
        .x1 = MIN(a.x1, b.x1),
        .y0 = MAX(a.y0, b.y0),
        .y1 = MIN(a.y1, b.y1),
    };

    struct _rect_diffs diff;

    const struct BLBoxI leftmost = a.x0 < b.x0 ? a : b;
    diff.left_full = (struct BLRectI) {
        .x = leftmost.x0,
        .w = intersection.x0 - leftmost.x0,
        .y = leftmost.y0,
        .h = leftmost.y1 - leftmost.y0,
    };

    const struct BLBoxI rightmost = a.x1 > b.x1 ? a : b;
    diff.right_full = (struct BLRectI) {
        .x = intersection.x1,
        .w = rightmost.x1 - intersection.x1,
        .y = rightmost.y0,
        .h = rightmost.y1 - rightmost.y0,
    };

    diff.top_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = MIN(a.y0, b.y0),
        .h = intersection.y1 - MIN(a.y0, b.y0),
    };

    diff.bottom_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = intersection.y1,
        .h = MAX(a.y1, b.y1) - intersection.y1,
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
    bl_context_stroke_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &st_surface->bl_path);
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
    const struct BLBoxI box_last_committed_buffer = st_surface->box_last_drawn;

    // TODO: Assert box_bounds fully surrounds box_to_draw
    assert(!SCRAN_BL_BOX_IS_INVERTED(box_to_draw));
    assert(!SCRAN_BL_BOX_IS_INVERTED(box_last_committed_buffer));
    // Equal boxes should have been skipped.
    assert(!_boxes_are_equal(box_to_draw, box_last_committed_buffer));


    // Our boxes must be enlarged by stroke radius amount to ensure the outline
    // is perfectly outside our capture area.
    const int drawn_box_inflation_px  = blend2d_stroke_ceil(BLCONTEXT_STROKE_RADIUS);
    // Our dirty rects must encompass the entire drawn stroke
    const int diffed_box_inflation_px = blend2d_stroke_ceil(drawn_box_inflation_px + BLCONTEXT_STROKE_RADIUS);

    // _box_bounds in particular is not supposed to have an outline in the
    // first place, but moving it out of frame is simpler than manipulating our
    // BLContext stroke rule. (It will never reach our dirty rects anyways.)
    struct BLBoxI box_bounds__outline_inflation     = get_blboxi_inflated(box_bounds,        drawn_box_inflation_px);
    struct BLBoxI box_to_draw__outline_inflation    = get_blboxi_inflated(box_to_draw,       drawn_box_inflation_px);

    bl_path_add_box_i(&st_surface->bl_path, &box_bounds__outline_inflation,  BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&st_surface->bl_path, &box_to_draw__outline_inflation, BL_GEOMETRY_DIRECTION_NONE);

    struct BLBoxI box_already_drawn__diff_inflation = get_blboxi_inflated(box_last_committed_buffer, diffed_box_inflation_px);
    struct BLBoxI box_to_draw__diff_inflation       = get_blboxi_inflated(box_to_draw,       diffed_box_inflation_px);

    const struct _rect_diffs rect_diffs = get_box_diffs_as_rects(box_to_draw__diff_inflation, box_already_drawn__diff_inflation);


    _draw_and_damage_region(st_surface, st_buffer, rect_diffs.left_full);
    _draw_and_damage_region(st_surface, st_buffer, rect_diffs.right_full);
    _draw_and_damage_region(st_surface, st_buffer, rect_diffs.top_remaining);
    _draw_and_damage_region(st_surface, st_buffer, rect_diffs.bottom_remaining);
    st_buffer->box_currently_drawn = box_to_draw;

    // NOTE: Don't reset the BLContext here, unless intending to fully
    // re-initialize it. Its state is initialized outside of this ::frame
    // event loop. Shouldn't need flushing either unless doing async.
    bl_path_reset(&st_surface->bl_path);
    bl_context_flush(&st_buffer->bl_ctx, BL_CONTEXT_FLUSH_NO_FLAGS);
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
    const struct BLBoxI box_previously_committed = st_output->surface.box_last_drawn;

    if (_boxes_are_equal(normalized_box_to_draw, box_previously_committed)) {
        goto go_next;
    }


    st_buffer->busy = true;


    draw_frame_and_damage_buffer(
        &st_output->surface,
        st_buffer,
        normalized_box_to_draw,
        st_output->selection_ctx.bl_box_bounds
    );
    st_output->surface.box_last_drawn = normalized_box_to_draw;

    wl_surface_attach(st_output->surface.wl_surface, st_buffer->wl_buffer, 0, 0);
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, st_output->surface.wl_surface),
        &presentation_feedback_listener,
        st_buffer
    );
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

