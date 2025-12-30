#include <stdio.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "state.h"
#include "wayland-event-handlers.h"

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

static void
draw_frame(
    struct client_state *state,
    struct client_state_surface_buffer *st_buffer
) {
    // XXX TEST TODO: Improve draw_frame

    struct client_state_selection_blend2d *bl = &state->selection.bl;
    struct BLPoint origin = { 0, 0 };

    // TODO: Only write and mark damage where needed
    memset(st_buffer->data, 0, state->surface.buf_size);

    bl_context_begin(&bl->ctx, &st_buffer->bl_img, NULL);

    bl_path_add_box_i(&bl->path, &bl->box_outer, BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&bl->path, &bl->box, BL_GEOMETRY_DIRECTION_NONE);
    bl_context_set_fill_rule(&bl->ctx, BL_FILL_RULE_EVEN_ODD);
    bl_context_set_fill_style_rgba32(&bl->ctx, 0x88888888);
    bl_context_fill_path_d(&bl->ctx, &origin, &bl->path);

    fprintf(
        stderr, "box: x0=%d, x1=%d, y0=%d, y1=%d\n",
        bl->box.x0, bl->box.x1, bl->box.y0, bl->box.y1
    );

    bl_context_end(&bl->ctx);
    bl_path_reset(&bl->path);
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

    // Destroy callback manually and request new frame "recursively"
    // TODO: Should this be done from main?
    wl_callback_destroy(callback);
    wl_callback_add_listener(
        wl_surface_frame(state->surface.surface),
        &surface_frame_callback_listener,
        state
    );

    if (st_buffer == NULL) {
        fprintf(stderr, "Both buffers busy...\n");
        // TODO: Restructure to not need to remember this for every fail condition
        wl_surface_commit(state->surface.surface);
        return;
    }
    // XXX TEST: This will likely end up staying, though...
    if (state->selection.selection_state == SELECTION_NONE) {
        // TODO: Restructure to not need to remember this for every fail condition
        wl_surface_commit(state->surface.surface);
        return;
    }

    st_buffer->busy = true;

    draw_frame(state, st_buffer);
    wl_surface_attach(state->surface.surface, st_buffer->buffer, 0, 0);

    // TODO: Calculate damage area to not re-draw entire surface every frame
    //       Probably call this within draw_frame?
    wl_surface_damage_buffer(
        state->surface.surface,
        0,
        0,
        state->surface.width,
        state->surface.height
    );
    wl_surface_commit(state->surface.surface);
}

struct wl_callback_listener surface_frame_callback_listener = {
    .done = surface_frame_callback_handler
};

