#include <assert.h>
#include <stdatomic.h>

#include <ext-image-copy-capture-v1.h>

#include "capture.h"
#include "event-handlers.h"
#include "freezeframe.h"
#include "state.h"


static inline void
capture_create_buffer_area_context(
    const struct scran_output *output,
    const struct capture_session_context *session,
    const struct capture_frame_context *frame_ctx,
    struct capture_buffer_area_context *buffer_area_ctx
) {
    buffer_area_ctx->area_px = capture_get_selection_as_capture_buffer_area_px(
        &output->capture,
        session,
        frame_ctx
    );
    buffer_area_ctx->area_start_address = capture_get_area_start_address(
        session,
        frame_ctx,
        &buffer_area_ctx->area_px
    );
    buffer_area_ctx->source_row_bytes =
        session->pixel_stride * session->source_dimensions_px.x;
}


static void
handle_image_copy_capture_frame_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;
    frame_ctx->source_transform = transform;
}


static void
handle_image_copy_capture_frame_damage(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    struct capture_frame_context *frame_ctx = data;
    capture_grow_tracked_damage(frame_ctx, x, y, width, height);
}


static void
handle_image_copy_capture_frame_presentation_time(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    struct capture_frame_context *frame_ctx = data;

    // XXX: Will overflow at tv_sec > ~584.9 years...
    const int64_t tv_sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = tv_sec_to_nsec + tv_nsec;
}


static void
handle_image_copy_capture_frame_ready(
    void *data,
    struct ext_image_copy_capture_frame_v1 *wl_frame
) {
    ext_image_copy_capture_frame_v1_destroy(wl_frame);

    struct capture_frame_context *frame_ctx = data;
    struct scran_output          *output    = frame_ctx->output;
    frame_ctx->frame = NULL;

    const bool image_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
    const bool video_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
    const bool freezeframe_requested = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME;
    assert(image_requested || video_requested || freezeframe_requested);
    frame_ctx->consumers = 0;

    if (freezeframe_requested) { // TODO: unlikely()
        freezeframe_capture_handle_frame_ready(output);
    }

    if (image_requested || video_requested) {
        const struct capture_session_context *session = &output->capture.session.session_ctx;
        struct capture_buffer_area_context buffer_area_ctx;
        capture_create_buffer_area_context(output, session, frame_ctx, &buffer_area_ctx);

        if (image_requested) {
            capture_image_write_image(output, session, frame_ctx, &buffer_area_ctx);
            capture_image_finish(output);
        }

        if (video_requested) {
            if (blboxi_intersects(buffer_area_ctx.area_px, frame_ctx->capture_buffer_damage_area_px)) {
                if (!capture_video_write_video_frame(output, frame_ctx, session, &buffer_area_ctx)) {
                    output->capture.video_end_requested = true;
                }
            }

            // NOTE: We do this check *after* writing the incoming frame. This ensures
            // that the video will not be cut short at the end if we're only capturing
            // frames on demand (with variable framerate) and nothing has changed for
            // the last x amount of time.
            // Forcing some compositor/surface damage when signaling to end the capture
            // should trigger the necessary final frame.
            //
            // TODO: Go through uses of capturing_video to check for redundancy now
            // that we have a global state, with e.g. `.exit_requested`.
            if (output->capture.video_end_requested || g_state.exit_requested) {
                capture_video_finish(output);
            } else {
                // TODO: avio_flush ?
                capture_request_frame(&output->capture.session, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO, NULL);
            }
        }
    }

    frame_ctx->capture_buffer_damage_area_px = (BLBoxI){0};
}


static void
handle_image_copy_capture_frame_failed(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct capture_frame_context *frame_ctx = data;
    struct scran_output          *output    = frame_ctx->output;
    frame_ctx->frame = NULL;

    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        freezeframe_capture_handle_failed(output, reason);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        capture_image_finish(output);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        // TODO: Retry a few times?
        capture_video_finish(output);
    }

    frame_ctx->consumers = 0;
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = handle_image_copy_capture_frame_presentation_time,
    .ready = handle_image_copy_capture_frame_ready,
    .failed = handle_image_copy_capture_frame_failed,
};
