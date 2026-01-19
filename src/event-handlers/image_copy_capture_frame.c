#include <unistd.h>
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "util/blend2d.h"


static void
handle_image_copy_capture_frame_transform__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;

    // TODO: What is this transform representing?
    //           It is separate from output::geometry's transform.
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

    // XXX TODO IMPORTANT: Implement this and add flag to enable damage-based capture
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
    const uint64_t sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = sec_to_nsec + tv_nsec;
}

// TODO:
//  - Can we fully avoid capturing the overlay (beyond just 
//    making sure it's out of frame) ?
//  - Either assert width/height isn't 0 (and enforce in selection logic)
//    or handle it properly here
//  - Allow resizing capture frame during recording
//     - Should be built around sws_scale and/or encoder filters,
//       whichever will be in use for cropping and scaling whenever
//       resizing gets implemented.
//
//  - Perform libav allocations in meminit
//       NOTE: Read up on the libav refcount mechanisms first.
//       https://www.ffmpeg.org/doxygen/trunk/group__lavc__encdec.html
//
static void
handle_image_copy_capture_frame_ready__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    struct capture_frame_context *frame_ctx = data;

    ext_image_copy_capture_frame_v1_destroy(frame);

    if (!frame_ctx->capturing) {
        goto end_capture;
    }

    // TODO: Find a way to clean this up and make it prettier

    // TODO: Clarify names, more in sync with start_capture names?
    const uint32_t source_row_bytes = frame_ctx->pixel_stride * frame_ctx->source_width_px;
    const uint8_t *const area_start_addr =
        frame_ctx->st_buffer.data
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;

    // XXX: We won't ever get planar src frame buffers... right..?
    const uint8_t *const _captured_planebufs[1] = { area_start_addr };
    const int _captured_linesizes[1] = { (int)source_row_bytes };
    // TODO: Is this necessary? Can we manage refs etc. more efficiently manually?
    av_frame_make_writable(frame_ctx->av_frame_encoded);
    // INFO: Width set during sws_ctx's init ensures crop
    // XXX: This sws_scale cropping "hack" needs to be updated to allow
    // non-equal length planes (e.g. YUV420).
    assert(frame_ctx->sws_ctx->src_w == blboxi_width_abs_unsafe(frame_ctx->capture_area_px));
    sws_scale(
        frame_ctx->sws_ctx,
        _captured_planebufs, _captured_linesizes, 0, blboxi_height_abs_unsafe(frame_ctx->capture_area_px),
        frame_ctx->av_frame_encoded->data, frame_ctx->av_frame_encoded->linesize
    );
    assert(frame_ctx->av_codec_ctx->time_base.den == NSEC_PER_SEC);
    frame_ctx->av_frame_encoded->pts = frame_ctx->presentation_time_nsec;

    int _retval_enc = avcodec_send_frame(frame_ctx->av_codec_ctx, frame_ctx->av_frame_encoded);
    assert(_retval_enc != AVERROR(EINVAL));
    AVPacket *av_packet = av_packet_alloc(); // XXX: Redundant with av_new_packet? And maybe vise-versa in our case?
    while (_retval_enc >= 0) {
        _retval_enc = avcodec_receive_packet(frame_ctx->av_codec_ctx, av_packet);
        assert(_retval_enc != AVERROR(EINVAL));

        // TODO: Are there other > 0 error codes it could return?
        if (_retval_enc == AVERROR_EOF || _retval_enc == AVERROR(EAGAIN)) {
            break;
        } else if (_retval_enc < 0) {
            eprintf("Error while encoding frame\n");

            // TODO: goto err
            return;
        }

        av_packet->stream_index = AV_FORMAT_STREAM_IDX_VIDEO;
        const AVStream *const _av_stream = frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO];
        assert(av_packet->stream_index == _av_stream->index);
        // NOTE: This is doing the work of av_packet_rescale_ts(), but with
        // asserts instead of conditionals. Just switch to that or to
        // if-statements if indeterminism becomes necessary.
        assert(av_packet->pts != AV_NOPTS_VALUE);
        av_packet->pts = av_rescale_q(av_packet->pts, frame_ctx->av_codec_ctx->time_base, _av_stream->time_base);
        assert(av_packet->dts != AV_NOPTS_VALUE);
        av_packet->dts = av_rescale_q(av_packet->dts, frame_ctx->av_codec_ctx->time_base, _av_stream->time_base);
        assert(av_packet->duration <= 0);

        // TODO: Look into conditionally using av_write_frame for sequential
        //       encoding
        av_interleaved_write_frame(frame_ctx->av_format_ctx, av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    // TODO: Double-check that freeing is safe wrt. encoder interleaving etc.,
    //       or whether we should unref instead
    av_packet_free(&av_packet);

    // TODO: avio_flush ?

    dispatch_video_capture_event_loop(frame_ctx);

    return;

end_capture:
    av_write_trailer(frame_ctx->av_format_ctx);
end_capture_err:
    // Note: Most (all?) of these are fine to call with null pointers, despite
    // the asserts
    assert(frame_ctx->av_format_ctx->pb);
    avio_close(frame_ctx->av_format_ctx->pb);
    assert(frame_ctx->av_format_ctx);
    avformat_free_context(frame_ctx->av_format_ctx);
    assert(frame_ctx->av_codec_ctx);
    avcodec_free_context(&frame_ctx->av_codec_ctx);
    assert(frame_ctx->av_frame_encoded);
    av_frame_free(&frame_ctx->av_frame_encoded);

    return;
}


// TODO: Maybe find some nicer naming convention than __video_capture etc., idk
struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__video_capture = {
    .transform = handle_image_copy_capture_frame_transform__video_capture,
    .damage = handle_image_copy_capture_frame_damage__video_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__video_capture,
    .ready = handle_image_copy_capture_frame_ready__video_capture,
};

