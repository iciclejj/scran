#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "print.h"


static void
handle_layer_surface_configure__selection(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Equal to post-transform output resolution if layer surface is anchored to every edge.
    uint32_t width_logical,
    uint32_t height_logical
) {
    struct scran_output                  *st_output         = data;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct scran_output_surface          *st_surface        = &st_output->selection_surface.surface;

    DEBUG("layer_surface::configure():\n");
    DEBUG("  width_logical: %d, height_logical: %d\n", width_logical, height_logical);

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (   st_surface->width_logical  != width_logical
        || st_surface->height_logical != height_logical
    ) {
        st_surface->width_logical = width_logical;
        st_surface->height_logical = height_logical;
        update_surface_scale_bufsize_viewport(st_output);

        reinit_scran_ui(&selection_surface->ui_ctx, selection_surface->surface.final_scale_factor_normalized);

        request_selection_surface_frame_callback(st_output);
    }
}


static void
handle_layer_surface_closed__selection(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    // struct scran_output_selectionSurface *selection_surface = data;
}


struct zwlr_layer_surface_v1_listener layer_surface_listener__selection = {
    .configure = handle_layer_surface_configure__selection,
    .closed = handle_layer_surface_closed__selection
};

