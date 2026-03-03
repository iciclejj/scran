#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"


static void
handle_layer_surface_configure(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Equal to post-transform output resolution if layer surface is anchored to every edge.
    uint32_t width_px_logical,
    uint32_t height_px_logical
) {
    struct scran_output *st_output = data;

    assert(width_px_logical == get_output_width_logical(st_output));
    assert(height_px_logical == get_output_height_logical(st_output));

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}


static void
handle_layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    struct scran_output *st_output = data;

    for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
        wl_buffer_destroy(st_output->surface.double_buffer[i].wl_buffer);
    }
}


struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_layer_surface_configure,
    .closed = handle_layer_surface_closed
};

