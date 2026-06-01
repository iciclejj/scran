#include "presentation-time.h"

#include "freezeframe.h"
#include "print.h"

static inline void
handle_presentation_feedback_presented__selection_transparent_for_freezeframe(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec,
    // If variable refresh rate:
    //   version == 1: must be 0.
    //   version >= 2: appropriate rate picked by the compositor, or 0.
    uint32_t refresh,
    uint32_t seq_hi,
    uint32_t seq_lo,
    uint32_t flags
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    DEBUG("::presented selection_transparent_for_freezeframe\n");

    struct scran_output *st_output = data;

    request_freezeframe_assume_callback_set(st_output);

    // HACK: Force some output damage, since some compositors (like Hyprland on
    // rapid consecutive freezeframe refreshes) may wait indefinitely for the
    // next capture frame if no damage is detected.
    //
    // Simply doing an empty commit, without damage, seems enough for Hyprland,
    // but we'll explicitly damage it just for good measure...
    wl_surface_damage_buffer(st_output->selection_surface.surface.wl_surface, 0, 0, 1, 1);
    wl_surface_commit(st_output->selection_surface.surface.wl_surface);
}

static inline void
handle_presentation_feedback_discarded__selection_transparent_for_freezeframe(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    DEBUG("::DISCARDED selection_transparent_for_freezeframe\n");

    // TODO(?):
    struct scran_output *st_output = data;
    request_freezeframe_assume_callback_set(st_output);
}

static inline void handle_presentation_feedback_sync_output__selection_transparent_for_freezeframe( void *data, struct wp_presentation_feedback *wp_presentation_feedback, struct wl_output *wl_output) { };

struct wp_presentation_feedback_listener presentation_feedback_listener__selection_transparent_for_freezeframe = {
    .presented = handle_presentation_feedback_presented__selection_transparent_for_freezeframe,
    .sync_output = handle_presentation_feedback_sync_output__selection_transparent_for_freezeframe,
    .discarded = handle_presentation_feedback_discarded__selection_transparent_for_freezeframe,
};
