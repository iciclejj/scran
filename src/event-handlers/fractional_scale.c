#include "fractional-scale-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "surface__selection.h"
#include "print.h"
#include "ui.h"


static inline enum scran_common_surface_update_handler_result
handle_fractional_scale_preferred_scale__common(
    struct scran_output_surface *st_surface,
    struct wp_fractional_scale_v1 *fractional_scale,
    uint32_t scale // denominator: 120
) {
    DEBUG("handle_fractional_scale_preferred_scale(): %f\n", _get_normalized_scaler(scale, 120));

    if (st_surface->fractional_scale_wp_120 != scale) {
        st_surface->fractional_scale_wp_120 = scale;
        update_surface_scale_and_size(st_surface);
        update_surface_viewport(st_surface);
        return SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UPDATED;
    }

    return SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UNCHANGED;
}

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
    struct scran_output_selectionSurface *selection_surface = data;

    enum scran_common_surface_update_handler_result ret = handle_fractional_scale_preferred_scale__common(
        &selection_surface->surface, fractional_scale, scale
    );

    if (ret == SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UPDATED) {
        struct scran_output *st_output = wl_container_of(selection_surface, st_output, selection_surface);
        reinit_scran_ui(&selection_surface->ui_ctx, selection_surface->surface.final_scale_factor_normalized);
        request_selection_surface_update(st_output);
    }
}


struct wp_fractional_scale_v1_listener fractional_scale_listener__selection = {
    .preferred_scale = handle_fractional_scale_preferred_scale__selection,
};
