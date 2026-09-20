#include "seat.h"
#include "selection-surface.h"
#include "state.h"
#include <wayland-util.h>


// TODO: Merge this with seat_set_mod_key_state() now?
void
seat_apply_mod_key_state(
    struct scran_output_selectionSurface *selection_surface,
    bool state
) {
    if(!selection_surface) {
        return;
    }

    struct scran_output *st_output = wl_container_of(selection_surface, st_output, selection_surface);

    // This is only used during video init, so just set this unconditionally
    // to avoid future possible sticky key bugs...
    st_output->capture.audio_disable_modifier_active = state;
}

void
seat_update_active_selection_surface(struct scran_seat *seat)
{
    // When multiple same-layer layer surface request ECXLUSIVE keyboard
    // interactivity, it is implementation-defined which surface gets keyboard
    // focus, so prioritize the pointer's focused surface, if available.
    //
    // COSMIC, as of recently started only giving keyboard focus to the "main"
    // display's selection surface.
    //
    // We control all our surfaces, so it doesn't matter if our "real" keyboard
    // focus is on a different surface/output.

    struct scran_output_selectionSurface *pointer_surface = seat->pointer_ctx.focused_selection_surface;
    struct scran_output_selectionSurface *keyboard_surface = seat->keyboard.focused_selection_surface;

    struct scran_output_selectionSurface *old_surface = seat->active_selection_surface;
    struct scran_output_selectionSurface *new_surface =
        pointer_surface
        ? (seat->pointer_ctx.pointer_focus_trusted ? pointer_surface : NULL)
        : keyboard_surface;

    if (new_surface != old_surface) {
        seat_apply_mod_key_state(old_surface, false);

        if (new_surface) {
            seat_apply_mod_key_state(new_surface, seat->mod_key_active);
            g_state.focused = true;
        } else {
            g_state.focused = false;
        }

        seat->active_selection_surface = new_surface;

        if (new_surface) {
            struct scran_output *new_output = wl_container_of(new_surface, new_output, selection_surface);
            request_selection_surface_frame_callback(new_output);
        }
        if (old_surface) {
            struct scran_output *prev_output = wl_container_of(old_surface, prev_output, selection_surface);
            request_selection_surface_frame_callback(prev_output);
        }
    }
}
