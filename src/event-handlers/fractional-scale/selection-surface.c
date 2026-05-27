#include "fractional-scale-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "ui.h"


// Wayland uses this for its ground truth to scale our layer surface.
//
// COSMIC, on the other hand, uses *either* wlr_output_management OR
// cosmic_output_management to do its calculations, depending on which happened
// to be used to set it. And seemingly with no way for the client to know which
// one was actually used... And *all* these three sources of scaling factors are
// incompatible with each others' denominators...
static inline void
handle_fractional_scale_preferred_scale__selection(
    void *data,
    struct wp_fractional_scale_v1 *fractional_scale,
    uint32_t scale // denominator: 120
) {
    struct scran_output                  *st_output         = data;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct scran_output_surface          *st_surface        = &selection_surface->surface;

    // The various scale events will often fire redundantly
    if (st_surface->fractional_scale_wp_120 != scale) {
        st_surface->fractional_scale_wp_120 = scale;

        update_surface_scale_bufsize_viewport(st_output);
        reinit_scran_ui(&selection_surface->ui_ctx, selection_surface->surface.final_scale_factor_normalized);
        request_selection_surface_frame_callback(st_output);
    }
}


struct wp_fractional_scale_v1_listener fractional_scale_listener__selection = {
    .preferred_scale = handle_fractional_scale_preferred_scale__selection,
};
