#include "fractional-scale-v1.h"

#include "state.h"
#include "state-util.h"

#include "print.h"


// Wayland uses this for its ground truth to scale our layer surface.
//
// COSMIC, on the other hand, uses *either* wlr_output_management OR
// cosmic_output_management to do its calculations, depending on which happened
// to be used to set it. And seemingly with no way for the client to know which
// one was actually used... And *all* these three sources of scaling factors are
// incompatible with each others' denominators...
static inline void
handle_fractional_scale_preferred_scale(
    void *data,
    struct wp_fractional_scale_v1 *fractional_scale,
    uint32_t scale // denominator: 120
) {
    struct scran_output *st_output = data;

    DEBUG("handle_fractional_scale_preferred_scale(): %f\n", _get_normalized_scaler(scale, 120));

    if (st_output->surface.fractional_scale_wp_120 != scale) {
        st_output->surface.fractional_scale_wp_120 = scale;
        update_selection_surface_scale_and_size(st_output);
        update_selection_surface_viewport(st_output);
    }
}


struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = handle_fractional_scale_preferred_scale,
};
