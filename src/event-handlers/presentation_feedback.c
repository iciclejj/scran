#include "presentation-time.h"

#include "state.h"
#include "state-util.h"
#include "util/blend2d.h"


extern struct scran g_state;


static inline void
handle_presentation_feedback_sync_output(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback,
    struct wl_output *wl_output
) {
    // Nothing to do here...
}


static inline void
handle_presentation_feedback_presented(
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

    struct scran_output_surface_buffer *st_buffer = data;
    struct scran_output *st_output = &g_state.outputs[get_containing_output_array_index(st_buffer)];

    // Must be synchronized at actual presentation time, and using the box that
    // was for certain used by the just-presented buffer. This should ensure
    // consistency across compositors, regardless of how they handle their
    // rendering.
    //
    // For example, at time of writing, naive assignment from within
    // surface_callback::frame works fine with sway, but not with COSMIC, which
    // is what triggered this change.
    //
    // TODO: This naive implementation is not very robust against
    // delayed/skiped/etc. frames. Probably a frame history and multi-buffered
    // capture is required, with currently available sync/protocol guarantees.
    st_output->capture.frame_ctx.capture_area_px = get_reverse_transform(
        st_buffer->box_currently_drawn,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );
}

static inline void
handle_presentation_feedback_discarded(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
}


struct wp_presentation_feedback_listener presentation_feedback_listener = {
    .presented = handle_presentation_feedback_presented,
    .sync_output = handle_presentation_feedback_sync_output,
    .discarded = handle_presentation_feedback_discarded,
};
