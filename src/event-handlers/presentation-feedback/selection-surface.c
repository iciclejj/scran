#include "presentation-time.h"

#include "state.h"
#include "state-util.h"
#include "capture.h"


static inline void
handle_presentation_feedback_sync_output__selection(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback,
    struct wl_output *wl_output
) {
    // Nothing to do here...
}


static inline void
handle_presentation_feedback_presented__selection(
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

    struct scran_output_selectionSurface_buffer *st_buffer = data;
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
    capture_update_selection(st_output, st_buffer->box_currently_drawn);

    // TODO: Move this into capture.c?
    {
        enum scran_fullscreen_ui_state *fullscreen_ui_state = &st_output->capture.fullscreen_ui_state;

        switch (*fullscreen_ui_state) {
        case SCRAN_FULLSCREEN_UI_SHOW_PENDING:
            if (st_buffer->drew_fullscreen_ui) {
                DEBUG("Presented fullscreen UI\n");
                *fullscreen_ui_state = SCRAN_FULLSCREEN_UI_SHOWN;
                capture_fullscreen_sync_ui_and_dispatch(st_output);
            }
            break;
        case SCRAN_FULLSCREEN_UI_SHOWN:
        case SCRAN_FULLSCREEN_UI_HIDE_PENDING:
        case SCRAN_FULLSCREEN_UI_NONE:
        case SCRAN_FULLSCREEN_UI_HIDDEN:
            break;
        }
    }
}

static inline void
handle_presentation_feedback_discarded__selection(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
}


struct wp_presentation_feedback_listener presentation_feedback_listener__selection = {
    .presented = handle_presentation_feedback_presented__selection,
    .sync_output = handle_presentation_feedback_sync_output__selection,
    .discarded = handle_presentation_feedback_discarded__selection,
};
