#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <assert.h>

#include "state.h"
#include "event-handlers.h"
#include "init.h"

#include "print.h"

static inline void
normalize_rect_i(struct BLRectI *rect)
{
    if (rect->w < 0) {
        rect->w = -rect->w;
        rect->x -= rect->w;
    }

    if (rect->h < 0) {
        rect->h = -rect->h;
        rect->y -= rect->h;
    }
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

static inline bool
_boxes_are_equal(BLBoxI a, BLBoxI b)
{
    return  a.x0 == b.x0 &&
            a.x1 == b.x1 &&
            a.y0 == b.y0 &&
            a.y1 == b.y1
    ;
}

static void
draw_frame_and_damage_buffer(
    struct scran_output *st_output,
    struct scran_output_surface_buffer *st_buffer
) {
    // XXX TODO: Improve draw_frame
    struct scran_output_selection_blend2d *bl = &st_output->selection.bl;
    const BLBoxI box_to_draw = bl->box;

    if (_boxes_are_equal(box_to_draw, st_buffer->bl_box_rendered)) {
        return;
    }

    struct BLPoint origin = { 0, 0 };
    const uint32_t buf_size = GET_SURFACE_BUF_SIZE(st_output->mode);

    bl_context_begin(&bl->ctx, &st_buffer->bl_img, NULL);

    // TODO: Only write and mark damage where needed
    bl_context_clear_all(&bl->ctx);

    bl_path_add_box_i(&bl->path, &bl->box_outer, BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&bl->path, &box_to_draw, BL_GEOMETRY_DIRECTION_NONE);
    bl_context_set_fill_rule(&bl->ctx, BL_FILL_RULE_EVEN_ODD);
    if (st_output->capture.frame_ctx.capturing_video) {
        // TODO: How is 88880000 hitting red and alpha?
        //           Need to set endianness flag?
        //       Show red border instead of red background
        bl_context_set_fill_style_rgba32(&bl->ctx, 0x88887A7A);
    } else {
        bl_context_set_fill_style_rgba32(&bl->ctx, 0x88888888);
    }
    bl_context_fill_path_d(&bl->ctx, &origin, &bl->path);

    // DEBUG(
    //     "box: x0=%d, x1=%d, y0=%d, y1=%d\n",
    //     box_to_draw.x0, box_to_draw.x1, box_to_draw.y0, box_to_draw.y1
    // );

    bl_context_end(&bl->ctx);
    st_buffer->bl_box_rendered = box_to_draw;
    bl_path_reset(&bl->path);

    // TODO: Calculate damage area to not re-draw entire surface every frame
    wl_surface_damage_buffer(
        st_output->surface.surface,
        0,
        0,
        st_output->mode.width_px,
        st_output->mode.height_px
    );
}

static inline struct BLBoxI
_get_reverse_transform(
    struct BLBoxI box,
    uint32_t source_width,
    uint32_t source_height,
    enum wl_output_transform transform
) {
    uint32_t tmp, tmp2;

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
        box.y0 = source_height - box.y1;
        box.x0 = source_width - box.x1;
         box.y1 = source_height - box.y0;
         box.x1 = source_width - box.x0;
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
    struct scran_output *st_output = data;

    // Destroy callback here and request new frame "recursively" within callback
    wl_callback_destroy(callback);

    if (st_output->selection.selection_state == SELECTION_EXIT_REQUESTED) {
        // Quit before requesting another frame
        return;
    }

    struct scran_output_surface_buffer *st_buffer = get_free_double_buffer(st_output);

    if (st_buffer == NULL ||
        st_output->selection.selection_state == SELECTION_NONE
    ) {
        #ifndef NDEBUG
            if (st_buffer == NULL) DEBUG("Both buffers busy...\n");
        #endif /* NDEBUG */

        goto go_next;
    }

    st_buffer->busy = true;

    // NOTE: Must be set here to sync with selection box rendering.
    //       Otherwise, rendered selection can lag behind the capture area,
    //        leading to f.ex. capture frame border spilling into the actual
    //        capture frame
    //       See also comment in scran_capture.
    st_output->capture.frame_ctx.capture_area_px = _get_reverse_transform(
        st_output->selection.bl.box,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );

    draw_frame_and_damage_buffer(st_output, st_buffer);
    wl_surface_attach(st_output->surface.surface, st_buffer->buffer, 0, 0);

go_next:
    wl_callback_add_listener(
        wl_surface_frame(st_output->surface.surface),
        &surface_frame_callback_listener,
        st_output
    );
    wl_surface_commit(st_output->surface.surface);
}

struct wl_callback_listener surface_frame_callback_listener = {
    .done = surface_frame_callback_handler
};

