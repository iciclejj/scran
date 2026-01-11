#include <stdio.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "state.h"
#include "wayland-event-handlers.h"
#include "init.h"

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

static inline struct client_state_output_surface_buffer *
get_free_double_buffer(struct client_state_output *st_output)
{
    struct client_state_output_surface_buffer *buffer =
        st_output->surface.double_buffer[0].busy
        ? &st_output->surface.double_buffer[1]
        : &st_output->surface.double_buffer[0]
    ;

    // fprintf(
    //     stderr, "get_free_double_buffer(): busy? buf_0=%d, buf_1=%d\n",
    //     state->surface.double_buffer[0].busy,
    //     state->surface.double_buffer[1].busy
    // );

    if (buffer->busy) {
        return NULL;
    }

    return buffer;
}

static void
draw_frame(
    struct client_state_output *st_output,
    struct client_state_output_surface_buffer *st_buffer
) {
    // XXX TEST TODO: Improve draw_frame

    struct client_state_output_selection_blend2d *bl = &st_output->selection.bl;
    struct BLPoint origin = { 0, 0 };
    const uint32_t buf_size = SURFACE_PIXEL_STRIDE * st_output->mode.width_px * st_output->mode.height_px;

    // TODO: Only write and mark damage where needed
    memset(st_buffer->data, 0, buf_size);

    bl_context_begin(&bl->ctx, &st_buffer->bl_img, NULL);

    bl_path_add_box_i(&bl->path, &bl->box_outer, BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&bl->path, &bl->box, BL_GEOMETRY_DIRECTION_NONE);
    bl_context_set_fill_rule(&bl->ctx, BL_FILL_RULE_EVEN_ODD);
    if (st_output->capture.capturing) {
        // TODO: How is 88880000 hitting red and alpha?
        //           Need to set endianness flag?
        //       Show red border instead of red background
        bl_context_set_fill_style_rgba32(&bl->ctx, 0x88887A7A);
    } else {
        bl_context_set_fill_style_rgba32(&bl->ctx, 0x88888888);
    }
    bl_context_fill_path_d(&bl->ctx, &origin, &bl->path);

    // fprintf(
    //     stderr, "box: x0=%d, x1=%d, y0=%d, y1=%d\n",
    //     bl->box.x0, bl->box.x1, bl->box.y0, bl->box.y1
    // );

    bl_context_end(&bl->ctx);
    bl_path_reset(&bl->path);
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
    // TODO: create_frame, create_something, ... ?

    struct client_state_output *st_output = data;
    struct client_state_output_surface_buffer *st_buffer = get_free_double_buffer(st_output);

    // Destroy callback manually and request new frame "recursively"
    // TODO: Should this be done from main?
    wl_callback_destroy(callback);
    wl_callback_add_listener(
        wl_surface_frame(st_output->surface.surface),
        &surface_frame_callback_listener,
        st_output
    );

    if (st_buffer == NULL) {
        fprintf(stderr, "Both buffers busy...\n");
        // TODO: Restructure to not need to remember this for every fail condition
        wl_surface_commit(st_output->surface.surface);
        return;
    }
    // XXX TEST: This will likely end up staying, though...
    if (st_output->selection.selection_state == SELECTION_NONE) {
        // TODO: Restructure to not need to remember this for every fail condition
        wl_surface_commit(st_output->surface.surface);
        return;
    }

    st_buffer->busy = true;

    // NOTE: Must be set here to sync with selection box rendering.
    //       Otherwise, rendered selection can lag behind the capture area,
    //        leading to f.ex. capture frame border spilling into the actual
    //        capture frame
    //       See also comment in client_state_capture.
    st_output->capture.capture_area = _get_reverse_transform(
        st_output->selection.bl.box,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );

    draw_frame(st_output, st_buffer);
    wl_surface_attach(st_output->surface.surface, st_buffer->buffer, 0, 0);

    // TODO: Calculate damage area to not re-draw entire surface every frame
    //       Probably call this within draw_frame?
    wl_surface_damage_buffer(
        st_output->surface.surface,
        0,
        0,
        st_output->mode.width_px,
        st_output->mode.height_px
    );
    wl_surface_commit(st_output->surface.surface);
}

struct wl_callback_listener surface_frame_callback_listener = {
    .done = surface_frame_callback_handler
};

