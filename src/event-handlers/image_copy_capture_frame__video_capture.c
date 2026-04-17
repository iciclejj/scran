#include <unistd.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"


extern struct scran g_state;


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
    const uint64_t tv_sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = tv_sec_to_nsec + tv_nsec;
}


static inline void
_write_video_frame(
    struct capture_frame_context *frame_ctx,
    AVPacket *av_packet // Encoded frame
) {
    assert(av_packet != NULL);

    const AVStream *const _av_stream = frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO];

    av_packet->stream_index = AV_FORMAT_STREAM_IDX_VIDEO;
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
    struct ext_image_copy_capture_frame_v1 *frame
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct capture_frame_context *frame_ctx = data;


    // Crop and convert

    uint8_t *const area_start_addr = get_capture_area_start_address(frame_ctx);
    // XXX: Can we make this const so area_start_addr can be const? It should
    // not change for the lifetime of this function (well, at least until the
    // next frame's dispatch at the end).
    frame_ctx->av_frame_captured->data[0] = area_start_addr;
    frame_ctx->av_frame_captured->pts = frame_ctx->presentation_time_nsec;
    int _ret_filter = av_buffersrc_write_frame(
            frame_ctx->av_filter_buffersrc_ctx,
            frame_ctx->av_frame_captured
    );
    assert(0 <= _ret_filter);

    _ret_filter = av_buffersink_get_frame(
            frame_ctx->av_filter_buffersink_ctx,
            frame_ctx->av_frame_to_encode
    );
    assert(0 <= _ret_filter);


    // Encode

    assert(av_frame_is_writable(frame_ctx->av_frame_to_encode));
    int _ret_enc = avcodec_send_frame(frame_ctx->av_codec_ctx, frame_ctx->av_frame_to_encode);
    assert(_ret_enc != AVERROR(EINVAL));
    av_frame_unref(frame_ctx->av_frame_to_encode);

    while (_ret_enc >= 0) {
        _ret_enc = avcodec_receive_packet(frame_ctx->av_codec_ctx, frame_ctx->av_packet);
        assert(_ret_enc != AVERROR(EINVAL));

        if (_ret_enc == AVERROR_EOF || _ret_enc == AVERROR(EAGAIN)) {
            break;
        } else if (_ret_enc < 0) {
            eprintf("Error while encoding frame\n");
            return; // TODO: goto err
        }

        _write_video_frame(frame_ctx, frame_ctx->av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    av_packet_unref(frame_ctx->av_packet);

    // NOTE: We do this check *after* writing the incoming frame. This ensures
    // that the video will not be cut short at the end if we're only capturing
    // frames on demand (with variable framerate) and nothing has changed for
    // the last x amount of time.
    // Calling capture_frame::damage_buffer() when signaling to end the capture
    // should trigger the necessary final frame.
    //
    // TODO: Go through uses of capturing_video to check for redundancy now
    // that we have a global state, with e.g. `.exit_requested`.
    if (!frame_ctx->capturing_video) {
        goto end_capture;
    } else if (g_state.exit_requested) {
        frame_ctx->capturing_video = false;
        goto end_capture;
    }

    // TODO: avio_flush ?

    request_video_capture_frame(
        frame_ctx,
        // TODO: Only damage what the capture area will be on next capture,
        // somehow? Will not be possible to determine from here, though,
        // at least not portably.
        0, 0, frame_ctx->source_width_px, frame_ctx->source_height_px
    );

    return;

end_capture:
    // Drain codec
    avcodec_send_frame(frame_ctx->av_codec_ctx, NULL);
    assert(frame_ctx->av_packet != NULL);
    while (avcodec_receive_packet(frame_ctx->av_codec_ctx, frame_ctx->av_packet) != AVERROR_EOF) {
        _write_video_frame(frame_ctx, frame_ctx->av_packet);
    }

    {
        struct scran_output_capture *const st_capture = wl_container_of(frame_ctx, st_capture, frame_ctx);
        struct scran_output *const st_output = wl_container_of(st_capture, st_output, capture);
        end_video_capture(st_output);
    }

    DEBUG("FINISHED RECORDING.\n");
    return;
}


void
handle_image_copy_capture_frame_failed__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__video_capture = {
    .transform = handle_image_copy_capture_frame_transform__video_capture,
    .damage = handle_image_copy_capture_frame_damage__video_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__video_capture,
    .ready = handle_image_copy_capture_frame_ready__video_capture,
    .failed = handle_image_copy_capture_frame_failed__video_capture,
};

