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
#include "event-handlers.h"


extern struct scran g_state;


bool
init_premem__freezeframe(
    struct scran_output *st_output
) {
    struct scran_output_subsurface       *st_subsurface     = &st_output->freezeframe.subsurface;

    {
        struct wl_surface *wl_surface = wl_compositor_create_surface(g_state.globals.compositor);

        // We call set_destination etc. in the ::scale handlers (and in postmem init)
        st_subsurface->viewport = wp_viewporter_get_viewport(g_state.globals.viewporter, wl_surface);

        assert(g_state.empty_wl_region != NULL);
        wl_surface_set_input_region(wl_surface, g_state.empty_wl_region);

        {
            struct scran_output_surface *parent_st_surface = &st_output->selection_surface.surface;
            assert(parent_st_surface->wl_surface != NULL);

            struct wl_subsurface *wl_subsurface = wl_subcompositor_get_subsurface(g_state.globals.subcompositor, wl_surface, parent_st_surface->wl_surface);
            wl_subsurface_place_below(wl_subsurface, parent_st_surface->wl_surface);
            st_subsurface->wl_subsurface = wl_subsurface;
        }

        st_subsurface->wl_surface = wl_surface;
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

    wp_viewport_destroy(freezeframe->subsurface.viewport);
    wl_subsurface_destroy(freezeframe->subsurface.wl_subsurface);
    wl_surface_destroy(freezeframe->subsurface.wl_surface);
    if (freezeframe->wl_capture_session) {
        ext_image_copy_capture_session_v1_destroy(freezeframe->wl_capture_session);
    }
}
