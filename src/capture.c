#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

#include "state.h"
#include "event-handlers.h"
#include "lib_interop.h"
#include "capture.h"
#include "util/blend2d.h"
#include "print.h"

// TODO: Let user set this
#define _FORMAT_MPEGTS_FILE_EXTENSION ".m2ts"
#define _FORMAT_MPEGTS_NAME "mpegts"
#define _CODEC_X264_NAME "libx264"

void
dispatch_video_capture_event_loop(struct capture_frame_context *frame_ctx)
{
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            *frame_ctx->session
        );
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener__video_capture, frame_ctx);
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        frame_ctx->st_buffer.buffer
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}

static void
_create_timestamped_filename(char filename[NAME_MAX])
{
    const time_t time_now = time(NULL);
    const struct tm *const time_now_tm = localtime(&time_now);
    strftime(filename, NAME_MAX, "test-capture_%Y%m%d-%H%M%S" _FORMAT_MPEGTS_FILE_EXTENSION, time_now_tm);
}

// TODO:
//  - Make all the libav code prettier.
//  - Use AVOutputFormat, AVCodecID, etc. directly, instead of strings?
//      - I.e. strings like "libx264", "mpegts", etc.
//  - Error checking
//  - Destruction/cleanup
//      - Don't forget avio_open
//
//  - Encoding parameters:
//      - Let user override the encoding parameters
//      - Decide on good defaults
//      - Store defaults in a const struct (or whatever format libav prefers)
//
static inline bool
init_ffmpeg(struct client_state_output *st_output)
{
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;

    const int width_px_captured = blboxi_width_abs_unsafe(frame_ctx->capture_area_px);
    const int height_px_captured = blboxi_height_abs_unsafe(frame_ctx->capture_area_px);
    const enum AVPixelFormat av_pixel_format_captured = wl_shm_format_to_ffmpeg(st_output->capture.shm_format);
    const int width_px_encoded = width_px_captured;
    const int height_px_encoded = height_px_captured;
    // NOTE: Some pixel formats (and some file formats), e.g. YUV420P, require
    //       even-numbered (or some other multiplier) height and/or width.
    //       Others, e.g. YUV444P and RGBA32, do not.
    const enum AVPixelFormat av_pixel_format_encoded = AV_PIX_FMT_YUV444P;


    // SwsContext
    const enum SwsFlags sws_flags = SWS_FAST_BILINEAR;
    // TODO: Use lower-level functions than sws_getContext ?
    //       Or getCachedContext?
    frame_ctx->sws_ctx = sws_getContext(
        width_px_captured, height_px_captured, av_pixel_format_captured,
        width_px_encoded, height_px_encoded, av_pixel_format_encoded,
        // TODO: What is src/dstFilter ?
        sws_flags, NULL, NULL, NULL
    );


    //AVFrame (encoded)
    frame_ctx->av_frame_encoded = av_frame_alloc();
    frame_ctx->av_frame_encoded->width = width_px_captured;
    frame_ctx->av_frame_encoded->height = height_px_captured;
    frame_ctx->av_frame_encoded->format = av_pixel_format_encoded;
    // TODO:
    // "@warning: if frame already has been allocated, calling this function
    //  will leak memory. In addition, undefined behavior can occur in certain cases."
    //    - Seemingly refers to the buffers of the frame, not the AVFrame itself ?
    //        Maybe open an ffmpeg issue/PR that clarifies it, if true?
    av_frame_get_buffer(frame_ctx->av_frame_encoded, 0);


    // AVCodec
    const struct AVCodec *const codec = avcodec_find_encoder_by_name(_CODEC_X264_NAME);

    // AVCodecContext (encoder)
    frame_ctx->av_codec_ctx = avcodec_alloc_context3(codec);
    frame_ctx->av_codec_ctx->width = width_px_captured;
    frame_ctx->av_codec_ctx->height = height_px_captured;
    // TODO: Variable framerate
    //       Is output::mode framerate_mhz same as the capture framerate?
    frame_ctx->av_codec_ctx->framerate = (AVRational){st_output->mode.refresh_rate_mHz, MILLIHZ_PER_HZ};
    // INFO: Using NSEC_PER_SEC due to (wayland's) frame::presentation_time()
    // giving time with nanosecond precision.
    frame_ctx->av_codec_ctx->time_base = (AVRational){1, NSEC_PER_SEC};
    frame_ctx->av_codec_ctx->pix_fmt = frame_ctx->av_frame_encoded->format;
    frame_ctx->av_codec_ctx->bit_rate = 20 * BITS_PER_MEGABIT;
    // XXX TODO: Figure out good default options for predicted frames
    frame_ctx->av_codec_ctx->max_b_frames = 0;
    frame_ctx->av_codec_ctx->gop_size = 0;
    avcodec_open2(frame_ctx->av_codec_ctx, codec, NULL);


    // AVFormatContext
    char filename[NAME_MAX];
    _create_timestamped_filename(filename);
    avformat_alloc_output_context2(&frame_ctx->av_format_ctx, NULL, _FORMAT_MPEGTS_NAME, filename);


    // AVStream
    AVStream *_av_stream = avformat_new_stream(frame_ctx->av_format_ctx, codec);
    assert(_av_stream == frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO]);
    // NOTE: Requested time_base. Final time_base will have been selected by
    // libav after write_header. av_packet_rescale_ts() exists to convert from
    // Encoder timestamps to Format timestamps prior to handing off the packet.
    // TODO: Should we set this to framerate? To encoder time_base?
    _av_stream->time_base = frame_ctx->av_codec_ctx->framerate;
    avcodec_parameters_from_context(_av_stream->codecpar, frame_ctx->av_codec_ctx);


    assert(!((frame_ctx->av_format_ctx)->oformat->flags & AVFMT_NOFILE));
    avio_open(&(frame_ctx->av_format_ctx)->pb, filename, AVIO_FLAG_WRITE);
    if (0 > avformat_write_header(frame_ctx->av_format_ctx, NULL)) {
        eprintf("Failed to write file header (filename: %s)\n", filename);

        if (frame_ctx->av_format_ctx != NULL) {
            avformat_free_context(frame_ctx->av_format_ctx);
        }
        // TODO: Should AVCodec * be freed?

        return false;
    }

    return true;
}

bool
start_video_capture(struct client_state_output *st_output)
{
    // TODO: Assert instead?
    if (st_output->capture.frame_ctx.capturing) {
        DEBUG("Already capturing...\n");
        return false;
    }

    // XXX: - Needs better asssert? Intent: make sure selection is complete and valid
    assert( st_output->selection.selection_state == SELECTION_COMPLETE
         || st_output->selection.selection_state == SELECTION_REBASING
         && st_output->selection.bl.box.x1
         && st_output->selection.bl.box.y1
    );

    struct capture_frame_context *const st_frame_ctx = &st_output->capture.frame_ctx;

    init_ffmpeg(st_output);

    // Get initial frame. Subsequent capture requests happen within
    // frame::ready, similar to the wl_surface callback event loop
    dispatch_video_capture_event_loop(&st_output->capture.frame_ctx);
    st_output->capture.frame_ctx.capturing = true;

    return true;
}
