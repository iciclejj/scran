#ifndef SCRAN_SEAT_H
#define SCRAN_SEAT_H


#include <wayland-client-core.h>

#include "state.h"
#include "state-util.h"


void seat_apply_mod_key_state(struct scran_seat *seat, struct scran_output_selectionSurface *selection_surface, bool state);
void seat_update_active_selection_surface(struct scran_seat *seat);

static inline void
seat_set_mod_key_state(struct scran_seat *seat, bool state) {
    seat_apply_mod_key_state(seat, seat->active_selection_surface, state);
    seat->mod_key_active = state;
}

static inline void
seat_set_focused_selection_surface_(
    struct scran_output_selectionSurface **dst,
    struct wl_surface *focused_surface
) {
    if (focused_surface == NULL) {
        *dst = NULL;
        return;
    }

    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
        if (focused_surface == selection_surface->surface.wl_surface) {
            *dst = selection_surface;
            return;
        }
    }

    // XXX: We do not have any other surfaces at the moment, so this should
    // never happen. This was changed to tracking the surface rather than
    // the output to make the scaling code more sane, despite only having one
    // surface per output at the moment. Change this as appropariate if adding
    // more surfaces.
    // We should still handle focused_surface == NULL appropriately in the rest
    // of the code, so this should still not be an error.
    //     XXX: This can currently trigger when dragging out of output bounds
    //     into a second, *left-hand-side* monitor.
    *dst = NULL;
    DEBUG("WARNING: wl_pointer::enter triggered with unknown surface (see comment in source.)\n");

    return;
}

static inline struct scran_output_selectionSurface *
seat_update_pointer_focus(struct scran_seat *seat, struct wl_surface *focused_surface) {
    struct scran_output_selectionSurface **pointer_focused_surface = &seat->pointer_ctx.focused_selection_surface;

    seat_set_focused_selection_surface_(&seat->pointer_ctx.focused_selection_surface, focused_surface);
    seat_update_active_selection_surface(seat);

    return *pointer_focused_surface;
}

static inline struct scran_output_selectionSurface *
seat_update_keyboard_focus(struct scran_seat *seat, struct wl_surface *focused_surface) {
    struct scran_output_selectionSurface **keyboard_focused_surface = &seat->keyboard.focused_selection_surface;

    seat_set_focused_selection_surface_(keyboard_focused_surface, focused_surface);
    seat_update_active_selection_surface(seat);

    return *keyboard_focused_surface;
}

#endif
