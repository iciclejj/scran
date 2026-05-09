#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "presentation-time.h"

#include "state-util.h"
#include "state.h"
#include "event-handlers.h"
#include "util/blend2d.h"
#include "surface__selection.h"
#include "capture.h"
#include "print.h"


extern struct scran g_state;


static inline struct scran_output_selectionSurface_buffer *
get_free_double_buffer(struct scran_output_selectionSurface *selection_surface)
{
    struct scran_output_selectionSurface_buffer *buffer =
        selection_surface->double_buffer[0].busy
        ? &selection_surface->double_buffer[1]
        : &selection_surface->double_buffer[0]
    ;

    if (buffer->busy) {
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

    struct scran_output                  *st_output         = data;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    if (!selection_surface->awaiting_frame_callback) {
        return;
    }

    selection_surface->awaiting_frame_callback = false;

    struct scran_output_selectionSurface_buffer *st_buffer = get_free_double_buffer(&st_output->selection_surface);

    if (st_buffer == NULL) {
        DEBUG("Both buffers busy...\n");
        request_selection_surface_frame_callback(st_output);
        return;
    }

    const struct BLBoxI capture_area = get_blboxi_deinverted(st_output->selection_ctx.box_px);
    assert(capture_area.x1 <= get_transformed_output_width(st_output));
    assert(capture_area.y1 <= get_transformed_output_height(st_output));
    const struct BLBoxI capture_area_previous_surface_commit = st_output->selection_surface.box_last_drawn;

    // XXX TODO: Does this even make any sense to have anymore, after the
    // on-demand redraw changes from ~a month ago? (Commented out due to the
    // check being too strict now with the new keymap ui. But check the above
    // before remaking it for ui keymap)
    //
    // if (!st_buffer->force_redraw && blboxi_are_equal(capture_area, capture_area_previous_surface_commit)) {
    //     goto done;
    // }

    st_buffer->busy = true;

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
        update_capture_area_with_selection(st_output, st_output->selection_ctx.box_px);
    }

    draw_selection_and_damage_buffer(
        &st_output->selection_surface,
        st_buffer,
        capture_area
    );
    st_output->selection_surface.box_last_drawn = capture_area;
    st_buffer->box_currently_drawn = capture_area;

    wl_surface_attach(st_output->selection_surface.surface.wl_surface, st_buffer->wl_buffer, 0, 0);
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, st_output->selection_surface.surface.wl_surface),
        &presentation_feedback_listener__selection,
        st_buffer
    );
    wl_surface_commit(st_output->selection_surface.surface.wl_surface);
}


struct wl_callback_listener selection_surface_frame_callback_listener = {
    .done = selection_surface_frame_callback_handler
};

