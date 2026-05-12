#include "fractional-scale-v1.h"

#include "state.h"
#include "freezeframe.h"
#include "event-handlers.h"


static inline void
handle_fractional_scale_preferred_scale__freezeframe(
    void *data,
    struct wp_fractional_scale_v1 *fractional_scale,
    uint32_t scale // denominator: 120
) {
    struct scran_output_freezeframe *freezeframe = data;

    DEBUG("fractional_scale::preferred_scale<Freezeframe>()\n");
    DEBUG("  scale: %d (120 denominator)\n", scale);

    if (freezeframe->surface.fractional_scale_wp_120 != scale) {
        freezeframe->surface.fractional_scale_wp_120 = scale;

        struct scran_output *st_output = wl_container_of(freezeframe, st_output, freezeframe);
        update_freezeframe_scale_size_viewport(st_output);
    }
}


struct wp_fractional_scale_v1_listener fractional_scale_listener__freezeframe = {
    .preferred_scale = handle_fractional_scale_preferred_scale__freezeframe,
};
