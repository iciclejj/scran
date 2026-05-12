#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "freezeframe.h"
#include "event-handlers.h"
#include "print.h"


static void
handle_layer_surface_configure__freezeframe(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Equal to post-transform output resolution if layer surface is anchored to every edge.
    uint32_t width_logical,
    uint32_t height_logical
) {
    struct scran_output_freezeframe *freezeframe = data;
    DEBUG("layer_surface::configure<Freezeframe>():\n");
    DEBUG("  width_logical: %d, height_logical: %d\n", width_logical, height_logical);

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (   freezeframe->surface.width_logical  != width_logical
        || freezeframe->surface.height_logical != height_logical
    ) {
        freezeframe->surface.width_logical  = width_logical;
        freezeframe->surface.height_logical = height_logical;

        struct scran_output *st_output = wl_container_of(freezeframe, st_output, freezeframe);
        update_freezeframe_scale_size_viewport(st_output);
    }
}


static void
handle_layer_surface_closed__freezeframe(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    // struct scran_output_freezeframe *freezeframe = data;
}


struct zwlr_layer_surface_v1_listener layer_surface_listener__freezeframe = {
    .configure = handle_layer_surface_configure__freezeframe,
    .closed = handle_layer_surface_closed__freezeframe,
};
