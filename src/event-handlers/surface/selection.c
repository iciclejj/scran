#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "state.h"
#include "state-util.h"
#include "capture.h"
#include "event-handlers.h"
#include "selection-surface.h"


static void
selection_surface_frame_callback_handler(
    void *data,
    struct wl_callback *callback,
    uint32_t time_ms
) {
    wl_callback_destroy(callback);

    struct scran_output *output = data;

    if (output->selection_surface.awaiting_frame_callback) {
        output->selection_surface.awaiting_frame_callback = false;
        draw_selection_and_commit(output);
    }
}


struct wl_callback_listener selection_surface_frame_callback_listener = {
    .done = selection_surface_frame_callback_handler
};


// For SCRAN_COMMIT_SYNC_AT_SURFACE_COMMIT_DISPLAY_SYNC. See scran_commit_sync().
static void
selection_surface_commit_sync_handler(
    void *data,
    struct wl_callback *callback,
    uint32_t callback_data
) {
    wl_callback_destroy(callback);

    struct scran_output_selectionSurface_buffer *st_buffer = data;
    struct scran_output *output = &g_state.outputs[get_containing_output_array_index(st_buffer)];

    // We can safely take box_currently_drawn here as long as we request the
    // ::done callback right after committing.
    capture_update_selection(output, st_buffer->box_currently_drawn);
}


struct wl_callback_listener selection_surface_commit_sync_listener = {
    .done = selection_surface_commit_sync_handler
};
