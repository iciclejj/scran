#include <stdio.h>

#include <wayland-client.h>

#include "state.h"

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

static inline struct client_state_surface_buffer *
get_free_double_buffer(struct client_state *state)
{
    struct client_state_surface_buffer *buffer =
        state->surface.double_buffer[0].busy
        ? &state->surface.double_buffer[1]
        : &state->surface.double_buffer[0]
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

void
surface_frame_callback_handler(
    void *data,
    struct wl_callback *callback,
    uint32_t time_ms
) {
    // TODO: create_frame, create_something, ... ?

    struct client_state *state = data;
    struct client_state_surface_buffer *st_buffer = get_free_double_buffer(state);

    // fprintf(stderr, "surface_frame_callback_handler\n");

    if (st_buffer == NULL) {
        // TODO: Don't print this...
        //       Figure out the intended dynamics and handle accordingly
        fprintf(stderr, "Both buffers busy...\n");
        return;
    }
    // XXX TEST: This will likely end up staying, though...
    if (!state->selection.selection_started) {
        return;
    }

    st_buffer->busy = true;

/*
    {
        // TEST
        state->surface.curr_color -= 0x01010101;

        state->surface.curr_width -= 2;
        if (state->surface.curr_width > state->surface.width) {
            state->surface.curr_width = state->surface.width;
        }
        state->surface.curr_height -= 4;
        if (state->surface.curr_height > state->surface.height) {
            state->surface.curr_height = state->surface.height;
        }

        memset(
            st_buffer->data,
            state->surface.curr_color,
            state->surface.buf_size
        );
        // fprintf(stderr, "surface_frame_callback_handler: Did memset\n");
    }
*/
    struct client_state_selection_blend2d *bl = &state->selection.bl;

    // XXX TEST TODO: Improve this
    bl_context_begin(&bl->ctx, &st_buffer->bl_img, NULL);
    memset(st_buffer->data, 0, state->surface.buf_size);

    // XXX TEST
    state->selection.bl.rect.x = bl->box.x0;
    state->selection.bl.rect.y = bl->box.y0;
    bl->rect.w = bl->box.x1 - bl->box.x0;
    bl->rect.h = bl->box.y1 - bl->box.y0;
    normalize_rect_i(&bl->rect);
    fprintf(
        stderr, "box: x0=%d, x1=%d, y0=%d, y1=%d\n",
        bl->box.x0, bl->box.x1, bl->box.y0, bl->box.y1
    );
    fprintf(
        stderr, "Rect: x=%d, y=%d, w=%d, h=%d\n",
        bl->rect.x, bl->rect.y, bl->rect.w, bl->rect.h
    );
    bl_context_fill_rect_i_rgba32(&bl->ctx, &bl->rect, 0x88888888);
    bl_context_end(&bl->ctx);

    wl_surface_attach(state->surface.surface, st_buffer->buffer, 0, 0);
    // TODO: Calculate damage area to not re-draw entire surface every frame
    wl_surface_damage_buffer(
        state->surface.surface,
        0,
        0,
        state->surface.width,
        state->surface.height
    );
    wl_surface_commit(state->surface.surface);

    // fprintf(stderr, "Committed buffer %p\n", st_buffer->buffer);
    // fprintf(stderr, "Color: %x\n", state->surface.curr_color);
}

struct wl_callback_listener surface_frame_callback_listener = {
    .done = surface_frame_callback_handler
};

