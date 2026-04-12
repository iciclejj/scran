#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "print.h"


static void
handle_layer_surface_configure(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Equal to post-transform output resolution if layer surface is anchored to every edge.
    uint32_t width_logical,
    uint32_t height_logical
) {
    struct scran_output_surface *st_surface = data;

    DEBUG("handle_layer_surface_configure():  width_logical: %d, height_logical: %d\n", width_logical, height_logical);

    if (   st_surface->width_logical != width_logical
        || st_surface->height_logical != height_logical
    ) {
        st_surface->width_logical = width_logical;
        st_surface->height_logical = height_logical;
        update_surface_scale_and_size(st_surface);
        update_surface_viewport(st_surface);
    }


    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
}


static void
handle_layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    struct scran_output_surface *st_surface = data;

    for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
        wl_buffer_destroy(st_surface->double_buffer[i].wl_buffer);
    }
}


struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_layer_surface_configure,
    .closed = handle_layer_surface_closed
};

