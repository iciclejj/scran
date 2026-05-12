#include <stdbool.h>
#include <assert.h>

#include <wayland-client-core.h>

#include "fractional-scale-v1.h"
#include "viewporter.h"
#include "ext-image-copy-capture-v1.h"
#include "wlr-layer-shell-unstable-v1.h"

#include "state.h"
#include "state-util.h"
#include "init.h"
#include "freezeframe.h"
#include "event-handlers.h"


extern struct scran g_state;


static inline void
init_freezeframe_layer_surface(
    struct scran_output *st_output
) {
    struct scran_output_surface *st_surface = &st_output->freezeframe.surface;

    st_surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        g_state.globals.layer_shell,
        st_surface->wl_surface,
        st_output->wl_output,
        // Maybe revert this to _TOP if it starts causing problems.
        // Ensuring the ordering of each surface commits seems reliable for now.
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "scran-freezeframe"
    );

    zwlr_layer_surface_v1_set_anchor(
        st_surface->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
    );
    zwlr_layer_surface_v1_set_exclusive_zone(
        st_surface->layer_surface,
        -1
    );
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_surface->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE
    );
    zwlr_layer_surface_v1_add_listener(st_surface->layer_surface, &layer_surface_listener__freezeframe, &st_output->freezeframe);
}

void
reinit_freezeframe_layer_surface(
    struct scran_output *st_output
) {
    struct scran_output_surface *st_surface = &st_output->freezeframe.surface;

    assert(st_surface->layer_surface != NULL);
    zwlr_layer_surface_v1_destroy(st_surface->layer_surface);

    init_freezeframe_layer_surface(st_output);
}


bool
init_premem__freezeframe(
    struct scran_output *st_output
) {
    struct scran_output_surface *st_surface = &st_output->freezeframe.surface;

    {
        struct wl_surface *wl_surface = wl_compositor_create_surface(g_state.globals.compositor);

        struct wp_fractional_scale_v1 *fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
            g_state.globals.fractional_scale_manager, wl_surface
        );
        wp_fractional_scale_v1_add_listener(fractional_scale, &fractional_scale_listener__freezeframe, &st_output->freezeframe);
        st_surface->fractional_scale = fractional_scale;

        // We call set_destination etc. in the ::scale handlers (and in postmem init)
        st_surface->viewport = wp_viewporter_get_viewport(g_state.globals.viewporter, wl_surface);

        assert(g_state.empty_wl_region != NULL);
        wl_surface_set_input_region(wl_surface, g_state.empty_wl_region);

        st_surface->wl_surface = wl_surface;
        init_freezeframe_layer_surface(st_output);

        wl_surface_commit(wl_surface);
    }


    st_output->freezeframe.wl_capture_session = ext_image_copy_capture_manager_v1_create_session(
        g_state.globals.image_copy_capture_manager,
        st_output->capture.source,
        g_state.options.disable_cursor_capture ? 0 : EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
    );
    st_output->freezeframe.shm_format = SCRAN_SHM_FORMAT_UNSET;

    ext_image_copy_capture_session_v1_add_listener(
        st_output->freezeframe.wl_capture_session,
        &image_copy_capture_session_listener__freezeframe,
        st_output
    );

    return true;
}

void
init_premem__freezeframe__destroy(
    struct scran_output *st_output
) {
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    zwlr_layer_surface_v1_destroy(freezeframe->surface.layer_surface);
    wl_surface_destroy(freezeframe->surface.wl_surface);
    wp_viewport_destroy(freezeframe->surface.viewport);
    wp_fractional_scale_v1_destroy(freezeframe->surface.fractional_scale);
    ext_image_copy_capture_session_v1_destroy(freezeframe->wl_capture_session);
}

bool
init_postmem__freezeframe(
    struct scran_output *st_output
) {
    // Update here in addition to within the ::scale handlers, since they may
    // have fired before the viewport was initialized.
    update_freezeframe_scale_size_viewport(st_output);

    return true;
}

void
init_postmem__freezeframe__destroy(
    struct scran_output *st_output
) {
    // Nothing to do here yet...
}
