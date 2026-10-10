#include <assert.h>
#include <stdatomic.h>

#include <ext-image-copy-capture-v1.h>

#include "capture.h"
#include "event-handlers.h"
#include "freezeframe.h"
#include "selection.h"
#include "state.h"


static inline void
capture_create_buffer_area_context(
    struct capture_view view,
    bool fullscreen,
    struct capture_buffer_area_context *buffer_area_ctx
) {
    const BLBoxI selection = fullscreen ? get_fullscreen_selection_box(view.output) : view.output->capture.selection_ctx_box_px;

    buffer_area_ctx->area_px            = capture_get_selection_as_capture_buffer_area_px(view, selection);
    buffer_area_ctx->area_start_address = capture_get_area_start_address(view, &buffer_area_ctx->area_px);
    buffer_area_ctx->source_row_bytes   = view.session_ctx->pixel_stride * view.session_ctx->source_dimensions_px.x;
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
    struct capture_frame_context *frame_ctx = data;
    const struct capture_view     view      = capture_view_from_frame(frame_ctx);
    struct scran_output          *output    = view.output;

    capture_destroy_frame(view);

    const bool image_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
    const bool video_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
    const bool freezeframe_requested = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME;
    assert(image_requested || video_requested || freezeframe_requested);
    frame_ctx->consumers = 0;

    // Reset before dispatching, so that the damage of frames requested during
    // dispatch isn't erased.
    const BLBoxI damage = frame_ctx->capture_buffer_damage_area_px;
    frame_ctx->capture_buffer_damage_area_px = (BLBoxI){0};

    if (freezeframe_requested) { // TODO: unlikely()
        freezeframe_capture_handle_frame_ready(view);
    }

    if (image_requested || video_requested) {
        if (image_requested) {
            bool fullscreen = output->capture.fullscreen_consumers.active & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
            struct capture_buffer_area_context buffer_area_ctx;
            capture_create_buffer_area_context(view, fullscreen, &buffer_area_ctx);

            capture_image_write_image(view, &buffer_area_ctx);
            capture_image_finish(output);
        }

        if (video_requested) {
            // A new session's first requested frame can hit the same presented
            // frame that the previous session already handled. If we don't skip
            // it, we could get a duplicated pts or write an old display state
            // with the new session's paint_cursors setting too early.
            // Potentially also a frame rendered with a new cursor image, if a
            // compositor behaves weird like that.
            if (frame_ctx->presentation_time_nsec < output->capture.session_switch_time_nsec) {
                const BLRectI unconsumed_damage = blboxi_to_blrecti(damage);
                capture_request_frame(
                    view, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO,
                    // TODO: Maybe clean up the request/damage functions so we don't need this ternary
                    blboxi_is_empty(damage) ? NULL : &unconsumed_damage
                );
            } else {
                bool fullscreen = output->capture.fullscreen_consumers.active & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
                struct capture_buffer_area_context buffer_area_ctx;
                capture_create_buffer_area_context(view, fullscreen, &buffer_area_ctx);

                if (blboxi_intersects(buffer_area_ctx.area_px, damage)) {
                    if (!capture_video_write_video_frame(view, &buffer_area_ctx)) {
                        output->capture.video_stage = SCRAN_VIDEO_STAGE_STOP_REQUESTED;
                    }
                }

                // NOTE: We do this check *after* writing the incoming frame. This ensures
                // that the video will not be cut short at the end if we're only capturing
                // frames on demand (with variable framerate) and nothing has changed for
                // the last x amount of time.
                // Forcing some compositor/surface damage when signaling to end the capture
                // should trigger the necessary final frame.
                if (output->capture.video_stage == SCRAN_VIDEO_STAGE_STOP_REQUESTED || g_state.exit_requested) {
                    capture_video_finish(output);
                } else {
                    // TODO: avio_flush ?
                    capture_request_frame(view, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO, NULL);
                }
            }
        }
    }
}


static void
handle_image_copy_capture_frame_failed(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    struct capture_frame_context *frame_ctx = data;
    struct capture_view           view      = capture_view_from_frame(frame_ctx);

    capture_destroy_frame(view);

    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        freezeframe_capture_handle_failed(view.output, reason);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        capture_image_finish(view.output);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        // TODO: Retry a few times?
        view.output->capture.ffmpeg_ctx.failure_mask |= SCRAN_VIDEO_FAILURE_VIDEO_STREAM;
        capture_video_finish(view.output);
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
