#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "presentation-time.h"

#include "state-util.h"
#include "state.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "selection.h"
#include "capture.h"
#include "print.h"


static inline struct scran_output_selectionSurface_buffer *
get_free_double_buffer(struct scran_output_selectionSurface *selection_surface)
{
    struct scran_output_selectionSurface_buffer *buffer =
        selection_surface->double_buffer[0].scran_wl_buffer.busy
        ? &selection_surface->double_buffer[1]
        : &selection_surface->double_buffer[0]
    ;

    if (buffer->scran_wl_buffer.busy) {
        return NULL;
    }

    return buffer;
}


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

    struct scran_output_selectionSurface_buffer *buffer = get_free_double_buffer(&output->selection_surface);

    if (buffer == NULL) {
        DEBUG("Both buffers busy...\n");
        request_selection_surface_frame_callback(output);
        return;
    }

    // This is the capture area that the rest of this function is assuming will
    // be in use for the frame in which this selection area is presented.
    const struct BLBoxI selection = selection_get_box_px(&output->selection_ctx);
    assert(selection.x1 <= get_transformed_output_width(output));
    assert(selection.y1 <= get_transformed_output_height(output));

    buffer->scran_wl_buffer.busy = true;

    // XXX HACK: Temporary (hopefully) workaround for regression introduced by
    // trying to fix cosmic and hyprland sync by assigning on
    // presentation_feedback::presented. Should be removed once the syncing
    // logic is more robust.
    if (   g_state.globals.cosmic_output_manager == NULL
        && g_state.globals.hypr_surface_manager == NULL
    ) {
        // XXX TODO: Check whether we're actually sway more robustly, and assign
        // it as part of our state. (So we don't need to assume the user is
        // running either cosmic or sway.)
        capture_update_selection(output, selection);
    }

    draw_selection_and_damage_buffer(output, buffer, selection);

    wl_surface_attach(output->selection_surface.surface.wl_surface, buffer->scran_wl_buffer.wl_buffer, 0, 0);
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, output->selection_surface.surface.wl_surface),
        &presentation_feedback_listener__selection,
        buffer
    );
    wl_surface_commit(output->selection_surface.surface.wl_surface);
    output->selection_surface.committed_selection = selection;
}


struct wl_callback_listener selection_surface_frame_callback_listener = {
    .done = selection_surface_frame_callback_handler
};
