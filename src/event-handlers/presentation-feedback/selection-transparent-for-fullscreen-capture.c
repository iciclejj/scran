#include "presentation-time.h"

#include "capture.h"
#include "print.h"


static void handle_presentation_feedback_sync_output__selection_transparent_for_fullscreen_capture(void *data, struct wp_presentation_feedback *wp_presentation_feedback, struct wl_output *wl_output) { };

static inline void
start_video_capture_or_unwind_fullscreen(struct scran_output *st_output) {
    if (!video_capture_start(st_output)) {
        end_fullscreen_capture(st_output);
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
    DEBUG("::presented selection_transparent_for_fullscreen_capture\n");

    struct scran_output *st_output = data;
    start_video_capture_or_unwind_fullscreen(st_output);
}

static void
handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_video_capture(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    DEBUG("::DISCARDED selection_transparent_for_fullscreen_capture\n");

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
    DEBUG("::presented selection_transparent_for_fullscreen_capture\n");

    struct scran_output *st_output = data;
    image_capture_start(st_output, st_output->capture.exit_after_capture);
}

static void
handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_capture(
    void *data,
    struct wp_presentation_feedback *wp_presentation_feedback
) {
    wp_presentation_feedback_destroy(wp_presentation_feedback);
    DEBUG("::DISCARDED selection_transparent_for_fullscreen_capture\n");

    // TODO(?):
    struct scran_output *st_output = data;
    image_capture_start(st_output, st_output->capture.exit_after_capture);
}

struct wp_presentation_feedback_listener presentation_feedback_listener__selection_transparent_for_fullscreen_image_capture = {
    .presented = handle_presentation_feedback_presented__selection_transparent_for_fullscreen_image_capture,
    .sync_output = handle_presentation_feedback_sync_output__selection_transparent_for_fullscreen_capture,
    .discarded = handle_presentation_feedback_discarded__selection_transparent_for_fullscreen_capture,
};
