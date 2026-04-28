#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "surface__selection.h"
#include "print.h"


static inline enum scran_common_surface_update_handler_result
_handle_layer_surface_configure__common(
    struct scran_output_surface *st_surface, // data
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Equal to post-transform output resolution if layer surface is anchored to every edge.
    uint32_t width_logical,
    uint32_t height_logical
) {
    DEBUG("handle_layer_surface_configure():  width_logical: %d, height_logical: %d\n", width_logical, height_logical);

    enum scran_common_surface_update_handler_result ret = SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UNCHANGED;

    if (   st_surface->width_logical != width_logical
        || st_surface->height_logical != height_logical
    ) {
        st_surface->width_logical = width_logical;
        st_surface->height_logical = height_logical;
        update_surface_scale_and_size(st_surface);
        update_surface_viewport(st_surface);
        ret = SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UPDATED;
    }

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    return ret;
}


static void
handle_layer_surface_configure__selection(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    // Equal to post-transform output resolution if layer surface is anchored to every edge.
    uint32_t width_logical,
    uint32_t height_logical
) {
    struct scran_output_selectionSurface *selection_surface = data;

    enum scran_common_surface_update_handler_result ret = _handle_layer_surface_configure__common(
        &selection_surface->surface, layer_surface, serial, width_logical, height_logical
    );

    if (ret == SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UPDATED) {
        struct scran_output *st_output = wl_container_of(selection_surface, st_output, selection_surface);
        reinit_scran_ui(&selection_surface->ui_ctx, selection_surface->surface.final_scale_factor_normalized);
        request_selection_surface_update(st_output);
    }
}


static void
handle_layer_surface_closed__selection(void *data, struct zwlr_layer_surface_v1 *layer_surface)
{
    struct scran_output_selectionSurface *selection_surface = data;

    // XXX TODO: Move this responsibility elsewhere
    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; i++) {
        wl_buffer_destroy(selection_surface->double_buffer[i].wl_buffer);
    }
}


struct zwlr_layer_surface_v1_listener layer_surface_listener__selection = {
    .configure = handle_layer_surface_configure__selection,
    .closed = handle_layer_surface_closed__selection
};

