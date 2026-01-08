#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "wayland-event-handlers.h"

static void
handle_layer_surface_configure(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    struct client_state *state = data;

    // TODO: Handle 0 height/width
    state->surface.height = height;
    state->surface.width = width;

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void
handle_layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    struct client_state *state = data;

    for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
        struct client_state_surface_buffer *buffer = &state->surface.double_buffer[i];

        munmap(buffer->data, state->surface.buf_size);
        wl_buffer_destroy(buffer->buffer);
    }
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_layer_surface_configure,
    .closed = handle_layer_surface_closed
};
