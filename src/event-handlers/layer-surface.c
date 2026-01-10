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
    uint32_t width_px_logical,
    uint32_t height_px_logical
) {
    struct client_state_output_surface *st_surface = data;

    // TODO: Handle 0 height/width
    st_surface->height_px = height_px_logical;
    st_surface->width_px = width_px_logical;

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void
handle_layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    struct client_state_output_surface *st_surface = data;

    for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
        struct client_state_output_surface_buffer *buffer = &st_surface->double_buffer[i];

        // XXX: MEMORY ALLOC/FREE HERE
        //          Alloc is in init/surface.c
        munmap(buffer->data, st_surface->buf_size);
        wl_buffer_destroy(buffer->buffer);
    }
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_layer_surface_configure,
    .closed = handle_layer_surface_closed
};
