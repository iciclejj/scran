#include <unistd.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>

#include "ext-image-copy-capture-v1.h"

#include "scranrot.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "util/util.h"


static void
handle_image_copy_capture_frame_transform__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;
    frame_ctx->source_transform = transform;
}


static void
handle_image_copy_capture_frame_damage__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    struct capture_frame_context *frame_ctx = data;

    video_capture_grow_tracked_damage(frame_ctx, x, y, width, height);
}


static void
handle_image_copy_capture_frame_presentation_time__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    struct capture_frame_context *frame_ctx = data;

    // XXX: Will overflow at tv_sec > ~584.9 years...
    const int64_t tv_sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = tv_sec_to_nsec + tv_nsec - frame_ctx->presentation_time_nsec_start;
}

static inline void
end_capture(
    struct capture_frame_context *frame_ctx
) {
    {
        struct scran_output_capture *const st_capture = wl_container_of(frame_ctx, st_capture, frame_ctx);
        struct scran_output *const st_output = wl_container_of(st_capture, st_output, capture);
        video_capture_finish(st_output);
    }

    frame_ctx->capturing_video = false;
    frame_ctx->video_end_requested = false;

    DEBUG("FINISHED RECORDING.\n");
}

static inline bool
do_handle_frame(
    struct capture_frame_context *frame_ctx,
    struct scran_output *st_output,
    BLBoxI capture_buffer_area_px
) {
    struct ffmpeg_context  *ffmpeg_ctx = &frame_ctx->ffmpeg_ctx;
    struct capture_session *session    = &st_output->capture.session;

    // Crop and convert
    {

        uint8_t *const area_start_addr = capture_get_area_start_address(frame_ctx, session, capture_buffer_area_px);
        // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be divisible by 2.
        // XXX TODO: Collect this bit zeroing logic somehow? (Duplicated in init_ffmpeg.)
        const int area_w_px = blboxi_width_abs_unsafe(capture_buffer_area_px) & ~0b1;
        const int area_h_px = blboxi_height_abs_unsafe(capture_buffer_area_px) & ~0b1;
        const uint32_t source_row_bytes = session->pixel_stride * session->source_dimensions_px.x;

        uint32_t  rgba32_shuffle = wl_shm_format_to_scranrot_yuv_rgba32_shuffle(st_output->capture.session.shm_format);
        if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
            eprintf("WARNING: Output's pixel format (%x) not recognized. Please report this as a bug. Attempting anyways...\n",
                    st_output->capture.session.shm_format);
            rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
        }

        // XXX: Scranrot does not support flipped transforms yet, so we just
        // record it flipped for now, rather than blocking capture entirely.
        enum wl_output_transform transform = wl_output_transform_without_flip(frame_ctx->source_transform);

        AVFrame *frame = ffmpeg_ctx->av_frame_to_encode;
        void *const frame_buffer = frame_ctx->img_data_2;

        assert(frame->width == get_transformed_width(area_w_px, area_h_px, transform));
        assert(frame->height == get_transformed_height(area_w_px, area_h_px, transform));

        if (!scranrot_transform_framebuffer_to_yuv420(
                area_start_addr, area_w_px, area_h_px, source_row_bytes,
                frame_buffer,
                rgba32_shuffle,
                wl_output_transform_to_scranrot(transform),
                &frame->data[0], &frame->linesize[0],
                &frame->data[1], &frame->linesize[1],
                &frame->data[2], &frame->linesize[2]
            )
        ) {
            eprintf("Error: scranrot failed to convert framebuffer to yuv\n");
            return false;
        }
        frame->pts = frame_ctx->presentation_time_nsec;
    }

    // Encode
    int _ret_enc = avcodec_send_frame(ffmpeg_ctx->av_codec_ctx, ffmpeg_ctx->av_frame_to_encode);
    assert(_ret_enc != AVERROR(EINVAL));
    while (_ret_enc >= 0) {
        _ret_enc = avcodec_receive_packet(ffmpeg_ctx->av_codec_ctx, ffmpeg_ctx->av_packet);
        assert(_ret_enc != AVERROR(EINVAL));

        if (_ret_enc == AVERROR_EOF || _ret_enc == AVERROR(EAGAIN)) {
            break;
        } else if (_ret_enc < 0) {
            eprintf("Error while encoding frame\n");
            return false;
        }

        video_capture_write_video_packet(frame_ctx, ffmpeg_ctx->av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    av_packet_unref(ffmpeg_ctx->av_packet);
    return true;
}

// TODO:
//  - Can we fully avoid capturing the overlay (beyond just
//    making sure it's out of frame) ?
//  - Either assert width/height isn't 0 (and enforce in selection logic)
//    or handle it properly here
//  - Allow resizing capture frame during recording
//  - Perform libav allocations in meminit
//       NOTE: Read up on the libav refcount mechanisms first.
//       https://www.ffmpeg.org/doxygen/trunk/group__lavc__encdec.html
//
static void
handle_image_copy_capture_frame_ready__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *this_frame
) {
    ext_image_copy_capture_frame_v1_destroy(this_frame);

    struct capture_frame_context *frame_ctx  = data;

    // XXX TODO: Just pass st_output to this handler.
    struct scran_output_capture *const st_capture = wl_container_of(frame_ctx, st_capture, frame_ctx);
    struct scran_output         *const st_output  = wl_container_of(st_capture, st_output, capture);
    const BLBoxI capture_buffer_area_px =
        capture_get_selection_as_capture_buffer_area_px(frame_ctx, &st_capture->session);

    if (blboxi_intersects(capture_buffer_area_px, frame_ctx->capture_buffer_damage_area_px) ) {
        if (!do_handle_frame(frame_ctx, st_output, capture_buffer_area_px)) {
            frame_ctx->video_end_requested = true;
        }
    }

    frame_ctx->capture_buffer_damage_area_px = (BLBoxI){0};

    // NOTE: We do this check *after* writing the incoming frame. This ensures
    // that the video will not be cut short at the end if we're only capturing
    // frames on demand (with variable framerate) and nothing has changed for
    // the last x amount of time.
    // Forcing some compositor/surface damage when signaling to end the capture
    // should trigger the necessary final frame.
    //
    // TODO: Go through uses of capturing_video to check for redundancy now
    // that we have a global state, with e.g. `.exit_requested`.
    if (frame_ctx->video_end_requested || g_state.exit_requested) {
        end_capture(frame_ctx);
        return;
    }

    // TODO: avio_flush ?

    struct ext_image_copy_capture_frame_v1 *next_frame = video_capture_create_frame(&st_output->capture);
    ext_image_copy_capture_frame_v1_capture(next_frame);
    st_output->capture.frame_ctx.frame = next_frame;
}


static void
handle_image_copy_capture_frame_failed__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct capture_frame_context *frame_ctx  = data;

    // TODO: Retry a few times?
    end_capture(frame_ctx);
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__video_capture = {
    .transform = handle_image_copy_capture_frame_transform__video_capture,
    .damage = handle_image_copy_capture_frame_damage__video_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__video_capture,
    .ready = handle_image_copy_capture_frame_ready__video_capture,
    .failed = handle_image_copy_capture_frame_failed__video_capture,
};
