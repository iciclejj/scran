#include "presentation-time.h"

#include "capture.h"
#include "state.h"


static void handle_presentation_feedback_sync_output__transparent_selection_capture(void *data, struct wp_presentation_feedback *wp_presentation_feedback, struct wl_output *wl_output) { };

static void
do_handle_presented(
    struct scran_output *output
) {
    assert(output->capture.fullscreen_ui_state == SCRAN_FULLSCREEN_UI_HIDE_PENDING);
    assert(output->selection_surface.disable_reason_mask & SCRAN_SELECTION_SURFACE_DISABLE_REASON_FULLSCREEN_HIDE);

    const enum scran_capture_frame_consumer_mask pending = output->capture.fullscreen_consumers.awaiting_ui_hide;
    output->capture.fullscreen_consumers.awaiting_ui_hide = 0;
    output->capture.fullscreen_ui_state = SCRAN_FULLSCREEN_UI_HIDDEN;
    DEBUG("Acquired fullscreen hide\n");

    capture_fullscreen_dispatch_pending_consumers(output, pending);
    capture_fullscreen_sync_ui_hide(output);
}

static void
handle_presentation_feedback_presented__transparent_selection_capture(
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
    struct scran_output *output = data;

    do_handle_presented(output);
}

static void
handle_presentation_feedback_discarded__transparent_selection_capture(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    struct scran_output *output = data;

    // FIXME: Check how well ::discarded is handled/respected across different
    // compositors, then either handle this properly, or document why not.
    do_handle_presented(output);
}

struct wp_presentation_feedback_listener presentation_feedback_listener__transparent_selection_capture = {
    .presented = handle_presentation_feedback_presented__transparent_selection_capture,
    .sync_output = handle_presentation_feedback_sync_output__transparent_selection_capture,
    .discarded = handle_presentation_feedback_discarded__transparent_selection_capture,
};
