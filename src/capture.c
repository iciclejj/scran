#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
#include <libavutil/opt.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "state-util.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "init.h"
#include "selection.h"
#include "options.h"


// TODO: Let user set this
#define _FORMAT_MP4_FILE_EXTENSION ".mp4"
// XXX: Seems like the underlying FFOutputFormat structs aren't exposed in the
// public API (e.g. ff_mp4_muxer etc.), so we must let libavformat run its
// "guessing"/scoring algorithm using a format-name string.
#define _FORMAT_MP4_NAME "mp4"
#define _CODEC_X264_NAME "libx264"


extern struct scran g_state;


void
init_wl_capture_frame__video(struct capture_frame_context *frame_ctx)
{
    frame_ctx->frame = ext_image_copy_capture_session_v1_create_frame(
        frame_ctx->wl_capture_session
    );
    ext_image_copy_capture_frame_v1_add_listener(frame_ctx->frame, &image_copy_capture_frame_listener__video_capture, frame_ctx);
    // TODO: Check ffmpeg's buffering behavior and maybe use ring buffer for
    // this, with a size that ensures frames still buffered by
    // avcodec/avfiltergraph etc. stay untouched.
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame_ctx->frame,
        frame_ctx->st_buffer.wl_buffer
    );
}


void
dispatch_video_capture_event_loop(struct capture_frame_context *frame_ctx)
{
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            frame_ctx->wl_capture_session
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


// TODO:
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

    // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be
    // divisible by 2. TODO: Also update selection area visuals to this width.
    const int width_px_captured = blboxi_width_abs_unsafe(frame_ctx->capture_area_px) & ~0b1;
    const int height_px_captured = blboxi_height_abs_unsafe(frame_ctx->capture_area_px) & ~0b1;
    const enum AVPixelFormat av_pixel_format_captured = wl_shm_format_to_ffmpeg(st_output->capture.shm_format);
    const AVRational av_framerate_captured = { st_output->mode.refresh_rate_mHz, MILLIHZ_PER_HZ };
    const AVRational av_time_base_captured = { 1, NSEC_PER_SEC };

    enum ScranAVTransposeDir av_transpose_direction =
        wl_output_transform_to_ffmpeg_transpose_dir__inverse(st_output->transform);

    const int width_px_converted = get_transformed_width(width_px_captured, height_px_captured, st_output->transform);
    const int height_px_converted = get_transformed_height(width_px_captured, height_px_captured, st_output->transform);
    // NOTE: Some pixel formats (and some file formats), e.g. YUV420P, require
    //       even-numbered (or some other multiplier) height and/or width.
    const enum AVPixelFormat av_pixel_format_converted = AV_PIX_FMT_YUV420P;


    // AVFrame (captured)
    frame_ctx->av_frame_captured = av_frame_alloc();
    frame_ctx->av_frame_captured->width = width_px_captured;
    frame_ctx->av_frame_captured->height = height_px_captured;
    frame_ctx->av_frame_captured->format = av_pixel_format_captured;
    // XXX: We won't ever get planar src frame buffers, right..?
    frame_ctx->av_frame_captured->linesize[0] = get_capture_stride(st_output);


    // AVFilter
    // TODO: Ensure filters are freed before clipboard mode and/or before
    // subsequent video captures. (And do the same for all the other ffmpeg
    // context objects as well)
    frame_ctx->av_filter_graph = avfilter_graph_alloc();

    // AVFilter: Source (receives av_frame_captured)
    frame_ctx->av_filter_buffersrc_ctx = avfilter_graph_alloc_filter(
            frame_ctx->av_filter_graph, avfilter_get_by_name("buffer"), "in"
    );
    av_opt_set_image_size(frame_ctx->av_filter_buffersrc_ctx,
            "video_size", width_px_captured, height_px_captured, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_pixel_fmt(frame_ctx->av_filter_buffersrc_ctx,
            "pix_fmt", av_pixel_format_captured, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q(frame_ctx->av_filter_buffersrc_ctx,
            "time_base", av_time_base_captured, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q(frame_ctx->av_filter_buffersrc_ctx,
            "pixel_aspect", (AVRational){1,4}, AV_OPT_SEARCH_CHILDREN);
    avfilter_init_dict(frame_ctx->av_filter_buffersrc_ctx, NULL);

    AVFilterContext *_sink_input_filter;
    if (av_transpose_direction == SCRAN_AV_TRANSPOSE_DIR_NORMAL
        || av_transpose_direction == SCRAN_AV_TRANSPOSE_DIR_UNSUPPORTED
    ) {
        _sink_input_filter = frame_ctx->av_filter_buffersrc_ctx;
    } else {
        // AVFilter: Transpose
        frame_ctx->av_filter_transpose_ctx = avfilter_graph_alloc_filter(
                frame_ctx->av_filter_graph, avfilter_get_by_name("transpose"), "transpose"
        );
        av_opt_set_int(frame_ctx->av_filter_transpose_ctx,
                "dir", av_transpose_direction, AV_OPT_SEARCH_CHILDREN);
        avfilter_init_dict(frame_ctx->av_filter_transpose_ctx, NULL);

        avfilter_link(frame_ctx->av_filter_buffersrc_ctx, 0,
                      frame_ctx->av_filter_transpose_ctx, 0);

        _sink_input_filter = frame_ctx->av_filter_transpose_ctx;
    }

    // AVFilter: Sink (writes into av_frame_converted)
    frame_ctx->av_filter_buffersink_ctx = avfilter_graph_alloc_filter(
            frame_ctx->av_filter_graph, avfilter_get_by_name("buffersink"), "out"
    );
    av_opt_set_array( frame_ctx->av_filter_buffersink_ctx,
            "pixel_formats", AV_OPT_SEARCH_CHILDREN,
            0, 1, AV_OPT_TYPE_PIXEL_FMT, &av_pixel_format_converted
    );
    avfilter_init_dict(frame_ctx->av_filter_buffersink_ctx, NULL);

    avfilter_link(_sink_input_filter, 0,
                  frame_ctx->av_filter_buffersink_ctx, 0);

    avfilter_graph_config(frame_ctx->av_filter_graph, NULL);


    // AVFrame (converted, ready to be fed to encoder)
    frame_ctx->av_frame_converted = av_frame_alloc();
    frame_ctx->av_frame_converted->width = width_px_converted;
    frame_ctx->av_frame_converted->height = height_px_converted;
    frame_ctx->av_frame_converted->format = av_pixel_format_converted;


    // AVCodec
    static const char *codec_fallbacks[] = {
        "libx264",      // requires GPL ffmpeg build
        "libopenh264",
        "mpeg4"
    };
    static const size_t len_codec_fallbacks = sizeof(codec_fallbacks) / sizeof(codec_fallbacks[0]);
    const struct AVCodec *codec = NULL;
    const char *codec_name = NULL;
    for (size_t i = 0; i < len_codec_fallbacks; ++i) {
        codec = avcodec_find_encoder_by_name(codec_fallbacks[i]);

        if (codec != NULL) {
            codec_name = codec_fallbacks[i];
            break;
        }
    }
    if (codec == NULL) {
        eprintf("Error: No supported encoder found. Please ensure the linked"
                " version of libavcodec was built with one of the supported"
                " codecs:\n");
        for (size_t i = 0; i < len_codec_fallbacks; ++i) {
            eprintf("%s\n", codec_fallbacks[i]);
        }
        return false;
    } else {
        assert(codec_name != NULL);
        eprintf("Using codec: %s\n", codec_name);
    }

    // AVCodecContext (encoder)
    frame_ctx->av_codec_ctx = avcodec_alloc_context3(codec);
    assert(frame_ctx->av_frame_converted->width != 0);
    assert(frame_ctx->av_frame_converted->height != 0);
    frame_ctx->av_codec_ctx->width = frame_ctx->av_frame_converted->width;
    frame_ctx->av_codec_ctx->height = frame_ctx->av_frame_converted->height;
    // TODO: Is output::mode framerate_mhz same as the capture framerate?
    frame_ctx->av_codec_ctx->framerate = av_framerate_captured;
    // INFO: Using NSEC_PER_SEC due to (wayland's) frame::presentation_time()
    // giving time with nanosecond precision.
    frame_ctx->av_codec_ctx->time_base = (AVRational){1, NSEC_PER_SEC};
    // TODO: Assert format matches av_frame_converted
    //       XXX: Also probably consistently use either the top-defined vars
    //       *or* the av_frame_converted properties.
    frame_ctx->av_codec_ctx->pix_fmt = frame_ctx->av_frame_converted->format;
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


    const char *output_filepath = NULL;
    if (g_state.options.output_to_stdout) {
        output_filepath = "pipe:1";
    } else if (g_state.options.output_path_has_constant_filename) {
        output_filepath = g_state.options.output_path;
    } else {
        scran_update_output_filepath(&g_state.options, _FORMAT_MP4_FILE_EXTENSION);
        output_filepath = g_state.options.output_path;
    }

    // AVFormat
    avformat_alloc_output_context2(&frame_ctx->av_format_ctx, NULL, _FORMAT_MP4_NAME, output_filepath);

    // AVStream
    AVStream *_av_stream = avformat_new_stream(frame_ctx->av_format_ctx, codec);
    assert(_av_stream == frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO]);
    // NOTE: Requested time_base. Final time_base will have been selected by
    // libav after write_header. av_packet_rescale_ts() exists to convert from
    // Encoder timestamps to Format timestamps prior to handing off the packet.
    // TODO: Should we set this to framerate? To encoder time_base?
    assert(frame_ctx->av_codec_ctx->framerate.num != 0);
    assert(frame_ctx->av_codec_ctx->framerate.den != 0);
    _av_stream->time_base = frame_ctx->av_codec_ctx->framerate;
    avcodec_parameters_from_context(_av_stream->codecpar, frame_ctx->av_codec_ctx);


    AVDictionary *opts = NULL;
    // TODO: If/when we implement strict non-variable framerate:
    //          Ensure keyframes/i-frames are frequent enough to take short videos.
    av_dict_set(&opts, "movflags", "frag_keyframe", 0);
    assert(!((frame_ctx->av_format_ctx)->oformat->flags & AVFMT_NOFILE));
    avio_open(&(frame_ctx->av_format_ctx)->pb, output_filepath, AVIO_FLAG_WRITE);
    if (0 > avformat_write_header(frame_ctx->av_format_ctx, &opts)) {
        eprintf("Failed to write file header (filepath: %s)\n", output_filepath);

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

    if (!init_ffmpeg(st_output)) {
        eprintf("Error: Failed to initialize ffmpeg libraries.\n");
        return false;
    }

    set_selection_surface_theme(st_output, SURFACE_THEME_VIDEO_CAPTURE);

    // Get initial frame. Subsequent capture requests happen within
    // frame::ready, similar to the wl_surface callback event loop
    init_wl_capture_frame__video(&st_output->capture.frame_ctx);

    // Ensure sure first frame is fully rendered
    ext_image_copy_capture_frame_v1_damage_buffer(
        st_output->capture.frame_ctx.frame, 0, 0, st_output->mode.width_px, st_output->mode.height_px
    );
    ext_image_copy_capture_frame_v1_capture(st_output->capture.frame_ctx.frame);
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
            frame_ctx->wl_capture_session
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

