#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "event-handlers.h"

static void
handle_layer_surface_configure(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Should be equal to output resolution if layer surface is anchored to
    // every edge. TODO: Check scale/transform interactions
    uint32_t width_px_logical,
    uint32_t height_px_logical
) {
    struct client_state_output *st_output = data;

    assert(st_output->mode.width_px == width_px_logical);
    assert(st_output->mode.height_px == height_px_logical);

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}

static void
handle_layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    struct client_state_output *st_output = data;

    for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
        wl_buffer_destroy(st_output->surface.double_buffer[i].buffer);
    }
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_layer_surface_configure,
    .closed = handle_layer_surface_closed
};
