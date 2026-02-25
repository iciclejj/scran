#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <stdatomic.h>
#include <sys/stat.h>

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
#include "init.h"

// TODO: Let user set this
#define _FORMAT_MP4_FILE_EXTENSION ".mp4"
// XXX: Seems like the underlying FFOutputFormat structs aren't exposed in the
// public API (e.g. ff_mp4_muxer etc.), so we must let libavformat run its
// "guessing"/scoring algorithm using a format-name string.
#define _FORMAT_MP4_NAME "mp4"
#define _CODEC_X264_NAME "libx264"

extern struct scran g_state;

void
dispatch_video_capture_event_loop(struct capture_frame_context *frame_ctx)
{
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            *frame_ctx->session
        );
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener__video_capture, frame_ctx);
    // TODO: Check ffmpeg's buffering behavior and maybe use ring buffer for
    // this, with a size that ensures frames still buffered by
    // avcodec/avfiltergraph etc. stay untouched.
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        frame_ctx->st_buffer.wl_buffer
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}


// TODO: Maybe optimize this a bit (and/or make it a bit cleaner somehow).
//       Also ensure string/array safety. Either asserts or live.
void
create_timestamped_filename(
    char filename_ret[CAPTURE_OUTPUT_FILENAME_MAX],
    const char file_extension[CAPTURE_OUTPUT_FILE_EXTENSION_MAX]
) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm time_now_tm;
    localtime_r(&ts.tv_sec, &time_now_tm);

    char *_filename = filename_ret;
    // TODO: Remove this eventually and just use asserts. Resulting filename
    // length is deterministic.
    size_t _name_max = NAME_MAX;

    const int chars_added_after_sec = strftime(_filename, _name_max, "scran-capture_%Y%m%d-%H%M%S", &time_now_tm);
    _filename += chars_added_after_sec;
    _name_max -= chars_added_after_sec;

    // INFO: Assumes 4 decimal points (10khz) is the smallest safe divisor that
    // doesn't risk file-overwriting during rapid consecutive screenshots.
    const long _tv_usec = ts.tv_nsec / 100000;
    const int chars_added_after_usec = snprintf(_filename, _name_max, ".%04ld", _tv_usec);
    _filename += chars_added_after_usec;
    _name_max -= chars_added_after_usec;

    // XXX: %z is a gnu extension. (Timezone offset.)
    const int chars_added_after_timezone = strftime(_filename, _name_max, "%z", &time_now_tm);
    _filename += chars_added_after_timezone;
    _name_max -= chars_added_after_timezone;

    snprintf(_filename, _name_max, "%s", file_extension);
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
init_ffmpeg(struct scran_output *st_output)
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
    // TODO: Figure out a good default qmin/qmax.
    //      NOTE: This is the largest factor influencing init_ffmpeg's time
    //      to finish (wide q-range => longer codec init time).
    frame_ctx->av_codec_ctx->qmin = 20;
    frame_ctx->av_codec_ctx->qmax = 30;
    avcodec_open2(frame_ctx->av_codec_ctx, codec, NULL);


    // AVFormatContext
    // XXX TODO: Refactor path-related things once we implement custom save-path
    // arg-parsing. Keep everything contained here until then, despite being
    // inefficient. Also needs better error handling etc.
    char filepath[PATH_MAX] = CAPTURE_OUTPUT_DEFAULT_DIRPATH "/";
    mkdir(filepath, 0755);
    const size_t _filename_offset = sizeof(CAPTURE_OUTPUT_DEFAULT_DIRPATH);
    assert(filepath[_filename_offset - 1] == '/');
    const char _file_extension[] = _FORMAT_MP4_FILE_EXTENSION;
    create_timestamped_filename(filepath + _filename_offset, _file_extension);
    avformat_alloc_output_context2(&frame_ctx->av_format_ctx, NULL, _FORMAT_MP4_NAME, filepath);


    // AVStream
    AVStream *_av_stream = avformat_new_stream(frame_ctx->av_format_ctx, codec);
    assert(_av_stream == frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO]);
    // NOTE: Requested time_base. Final time_base will have been selected by
    // libav after write_header. av_packet_rescale_ts() exists to convert from
    // Encoder timestamps to Format timestamps prior to handing off the packet.
    // TODO: Should we set this to framerate? To encoder time_base?
    _av_stream->time_base = frame_ctx->av_codec_ctx->framerate;
    avcodec_parameters_from_context(_av_stream->codecpar, frame_ctx->av_codec_ctx);

    AVDictionary *opts = NULL;
    // TODO: Ensure keyframes/i-frames are still frequent enough to take short
    // videos whenever default values get decided on. (Works well as of now.)
    av_dict_set(&opts, "movflags", "frag_keyframe", 0);
    assert(!((frame_ctx->av_format_ctx)->oformat->flags & AVFMT_NOFILE));
    avio_open(&(frame_ctx->av_format_ctx)->pb, filepath, AVIO_FLAG_WRITE);
    if (0 > avformat_write_header(frame_ctx->av_format_ctx, &opts)) {
        eprintf("Failed to write file header (filepath: %s)\n", filepath);

        if (frame_ctx->av_format_ctx != NULL) {
            avformat_free_context(frame_ctx->av_format_ctx);
        }
        // TODO: Should AVCodec * be freed?

        return false;
    }

    return true;
}

bool
start_video_capture(struct scran_output *st_output)
{
    // TODO: Assert instead?
    if (st_output->capture.frame_ctx.capturing_video) {
        DEBUG("Already capturing...\n");
        return false;
    }

    // XXX: - Needs better asssert? Intent: make sure selection is complete and valid
    assert( st_output->selection_ctx.selection_state == SELECTION_COMPLETE
         || st_output->selection_ctx.selection_state == SELECTION_REBASING
         && st_output->selection_ctx.bl_box.x1
         && st_output->selection_ctx.bl_box.y1
    );

    init_ffmpeg(st_output);

    set_surface_theme(st_output, SURFACE_THEME_VIDEO_CAPTURE);

    // Get initial frame. Subsequent capture requests happen within
    // frame::ready, similar to the wl_surface callback event loop
    dispatch_video_capture_event_loop(&st_output->capture.frame_ctx);
    st_output->capture.frame_ctx.capturing_video = true;
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    return true;
}

void
dispatch_image_capture_event(struct scran_output_capture *st_capture)
{
    struct capture_frame_context *frame_ctx = &st_capture->frame_ctx;

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            *frame_ctx->session
        );
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener__image_capture, st_capture);
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        frame_ctx->st_buffer.wl_buffer
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}


bool
start_image_capture(struct scran_output *st_output)
{
    // See TODO at call site
    assert(!st_output->capture.frame_ctx.capturing_video);

    dispatch_image_capture_event(&st_output->capture);
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    return true;
}
