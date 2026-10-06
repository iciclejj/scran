#include <assert.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "presentation-time.h"
#include "viewporter.h"

#include "state.h"
#include "state-util.h"
#include "cursor.h"
#include "selection.h"
#include "print.h"
#include "capture.h"
#include "selection-surface.h"
#include "freezeframe.h"
#include "init.h"


// TODO: Rename to set_selection_state_complete
void
selection_set_initialized(struct scran_output *output)
{
    assert(output->selection_ctx.selection_state == SELECTION_INITIALIZING
           || (g_state.options.have_custom_initial_selection && output->selection_ctx.selection_state == SELECTION_NONE));

    output->selection_ctx.selection_state = SELECTION_COMPLETE;

    // Reset button, since this function could have interrupted an ongoing
    // state-dependent action.
    g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;

    // Make sure this is initialized immediately, to not be dependent on
    // surface::frame being done, for example when using 'scran -eg'.
    //     TODO: Would be better to de-couple this somehow.
    capture_update_selection(output, selection_get_box_px(&output->selection_ctx));

    if (g_state.options.capture_and_exit_after_selection_init) {
        DEBUG("STARTING AUTOMATIC IMAGE CAPTURE\n");
        capture_image_start(output, true);
        scran_request_exit();
    }
}

static void
selection_surface_hide(struct scran_output *output)
{
    struct scran_output_surface *surface  = &output->selection_surface.surface;

    assert(SURFACE_SHM_FORMAT == WL_SHM_FORMAT_ARGB8888); // Alpha channel must not be ignored.
    wl_surface_attach(
        surface->wl_surface,
        g_state.transparent_single_pixel_buffer.wl_buffer, 0, 0
    );
    wp_viewport_set_source(
        surface->viewport,
        wl_fixed_from_int(0), wl_fixed_from_int(0), wl_fixed_from_int(1), wl_fixed_from_int(1)
    );
    wl_surface_damage_buffer(
        surface->wl_surface,
        0, 0, 1, 1
    );
    wl_surface_commit(
        surface->wl_surface
    );
    output->selection_surface.single_pixel_buffer_committed = true;
}

void
selection_surface_acquire_hide_then(
    struct scran_output *output,
    struct wp_presentation_feedback_listener *listener,
    enum scran_selection_surface_disable_reason reason
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    // Once the ::presented event has verified that the selection surface was
    // hidden, we start the capture from within there.
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, selection_surface->surface.wl_surface),
        listener,
        output
    );

    // Need to prevent any new or in-flight frame callbacks from cancelling out
    // our surface hiding
    selection_surface->disable_reason_mask |= reason;

    selection_surface_hide(output);
}

void
selection_surface_release_hide(
    struct scran_output *output,
    enum scran_selection_surface_disable_reason reason
) {
    output->selection_surface.disable_reason_mask &= ~reason;
    if (!output->selection_surface.disable_reason_mask) {
        assert(output->selection_surface.single_pixel_buffer_committed == true);
        draw_selection_and_commit(output);
    }
}

void
scran_focus_grab_for_output(struct scran_output *output)
{
    struct scran_output_surface *surface = &output->selection_surface.surface;

    // NULL sets an infinite region
    wl_surface_set_input_region(surface->wl_surface, NULL);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        surface->layer_surface,
        SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
    );
    wl_surface_commit(surface->wl_surface);

    // TODO: Make arm_selection_surface_frame_callback externally callable, so
    // that we avoid potential double-commit here?
    request_selection_surface_frame_callback(output);
}

void
scran_focus_grab()
{
    DEBUG("Grabbing focus\n");

    FOR_EACH_OUTPUT(i, output) {
        scran_focus_grab_for_output(output);
    }
}

void
scran_focus_release()
{
    DEBUG("Releasing focus\n");

    FOR_EACH_OUTPUT(i, output) {
        freezeframe_hide_if_showing(output);
    }

    FOR_EACH_OUTPUT(i, output) {
        struct scran_output_surface *surface = &output->selection_surface.surface;

        wl_surface_set_input_region(surface->wl_surface, g_state.empty_wl_region);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            surface->layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED
        );
        wl_surface_commit(surface->wl_surface);
    }
}
