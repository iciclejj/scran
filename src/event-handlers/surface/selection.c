#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "state.h"
#include "event-handlers.h"
#include "selection-surface.h"


static void
selection_surface_frame_callback_handler(
    void *data,
    struct wl_callback *callback,
    uint32_t time_ms
) {
    wl_callback_destroy(callback);

    struct scran_output                  *output            = data;
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    bool skip = selection_surface->disable_reason_mask || !selection_surface->awaiting_frame_callback;
    selection_surface->awaiting_frame_callback = false;

    if (skip) {
        return;
    }

    draw_selection_and_commit(output);
}


struct wl_callback_listener selection_surface_frame_callback_listener = {
    .done = selection_surface_frame_callback_handler
};
