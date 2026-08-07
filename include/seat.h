#ifndef SCRAN_SEAT_H
#define SCRAN_SEAT_H


#include <wayland-client-core.h>

#include "state-util.h"


static inline void
seat_update_focused_selection_surface(
    struct scran_output_selectionSurface **dst,
    struct wl_surface *focused_surface
) {
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


#endif
