#include "presentation-time.h"

#include "capture.h"
#include "state.h"


static void handle_presentation_feedback_sync_output__selection_transparent_for_fullscreen_capture(void *data, struct wp_presentation_feedback *wp_presentation_feedback, struct wl_output *wl_output) { };

static inline void
start_video_capture_or_unwind_fullscreen(struct scran_output *st_output)
{
    struct scran_output_capture *capture = &st_output->capture;

    // Escape may have already cancelled and unwound this pending start.
    if (!capture->fullscreen_video_pending) {
        return;
    }

    capture->audio_disable_modifier_active = capture->fullscreen_video_pending_audio_disabled;
    capture->fullscreen_video_pending_audio_disabled = false;
    capture->fullscreen_video_pending = false;

    if (g_state.exit_requested || !capture_video_start(st_output)) {
        capture_fullscreen_end(st_output, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO);
    }
}

static void
handle_presentation_feedback_presented__selection_transparent_for_fullscreen_video_capture(
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
    struct scran_output *st_output = data;
    start_video_capture_or_unwind_fullscreen(st_output);
}

static void
handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_video_capture(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    // TODO(?):
    struct scran_output *st_output = data;
    start_video_capture_or_unwind_fullscreen(st_output);
}

struct wp_presentation_feedback_listener presentation_feedback_listener__selection_transparent_for_fullscreen_video_capture = {
    .presented = handle_presentation_feedback_presented__selection_transparent_for_fullscreen_video_capture,
    .sync_output = handle_presentation_feedback_sync_output__selection_transparent_for_fullscreen_capture,
    .discarded = handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_video_capture,
};


static void
handle_presentation_feedback_presented__selection_transparent_for_fullscreen_image_capture(
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
    struct scran_output *st_output = data;
    capture_image_start(st_output, st_output->capture.exit_after_capture);
}

static void
handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_capture(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    // TODO(?):
    struct scran_output *st_output = data;
    capture_image_start(st_output, st_output->capture.exit_after_capture);
}

struct wp_presentation_feedback_listener presentation_feedback_listener__selection_transparent_for_fullscreen_image_capture = {
    .presented = handle_presentation_feedback_presented__selection_transparent_for_fullscreen_image_capture,
    .sync_output = handle_presentation_feedback_sync_output__selection_transparent_for_fullscreen_capture,
    .discarded = handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_capture,
};
