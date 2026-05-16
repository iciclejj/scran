#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
#include <libavutil/opt.h>
#include <libavutil/display.h>
#include <libavutil/version.h>
#include <libavformat/version.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "state-util.h"
#include "surface__selection.h"
#include "ui.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "init.h"
#include "selection.h"
#include "options.h"
#include "clipboard.h"
#include "portals.h"
#include "pipewires.h"


#define _FORMAT_MP4_FILE_EXTENSION ".mp4"
// XXX: Seems like the underlying FFOutputFormat structs aren't exposed in the
// public API (e.g. ff_mp4_muxer etc.), so we must let libavformat run its
// "guessing"/scoring algorithm using a format-name string.
#define _FORMAT_MP4_NAME "mp4"


extern struct scran g_state;


void
write_audio_packet(
    struct capture_frame_context *frame_ctx,
    AVPacket *pkt // Encoded frame
) {
    assert(pkt != NULL);

    const AVStream *const av_stream = frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_AUDIO];
    pkt->stream_index = AV_FORMAT_STREAM_IDX_AUDIO;
    assert(pkt->stream_index == av_stream->index);

    // NOTE: This is doing the work of av_packet_rescale_ts(), but with
    // asserts instead of conditionals. Just switch to that or to
    // if-statements if indeterminism becomes necessary.
    assert(pkt->pts != AV_NOPTS_VALUE);
    pkt->pts = av_rescale_q(pkt->pts, frame_ctx->av_codec_ctx_audio->time_base, av_stream->time_base);
    assert(pkt->dts != AV_NOPTS_VALUE);
    pkt->dts = av_rescale_q(pkt->dts, frame_ctx->av_codec_ctx_audio->time_base, av_stream->time_base);
    assert(pkt->duration != AV_NOPTS_VALUE);
    pkt->duration = av_rescale_q(pkt->duration, frame_ctx->av_codec_ctx_audio->time_base, av_stream->time_base);

    av_interleaved_write_frame(frame_ctx->av_format_ctx, pkt);
}

void
write_video_frame(
    struct capture_frame_context *frame_ctx,
    AVPacket *pkt // Encoded frame
) {
    assert(pkt != NULL);

    const AVStream *const _av_stream = frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO];

    pkt->stream_index = AV_FORMAT_STREAM_IDX_VIDEO;
    assert(pkt->stream_index == _av_stream->index);

    // NOTE: This is doing the work of av_packet_rescale_ts(), but with
    // asserts instead of conditionals. Just switch to that or to
    // if-statements if indeterminism becomes necessary.
    assert(pkt->pts != AV_NOPTS_VALUE);
    pkt->pts = av_rescale_q(pkt->pts, frame_ctx->av_codec_ctx->time_base, _av_stream->time_base);
    assert(pkt->dts != AV_NOPTS_VALUE);
    pkt->dts = av_rescale_q(pkt->dts, frame_ctx->av_codec_ctx->time_base, _av_stream->time_base);

    // We don't use durations for VFR.
    //     XXX: This gives a warning on the last frame before
    //     avformat_write_header(). Not sure if worth fixing.
    assert(pkt->duration <= 0);

    av_interleaved_write_frame(frame_ctx->av_format_ctx, pkt);
}

uint8_t *
get_capture_area_start_address(
    struct capture_frame_context *frame_ctx
) {
    return frame_ctx->st_buffer.data
         + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
         + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;
}

// `selection_box` has `scran_output_selectionContext.box` coordinate space!
void
update_capture_area_with_selection(
    struct scran_output *st_output,
    BLBoxI selection_box
) {
    st_output->capture.frame_ctx.capture_area_px = get_reverse_transform(
        selection_box,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );

    assert(st_output->capture.frame_ctx.capture_area_px.x1 <= st_output->mode.width_px);
    assert(st_output->capture.frame_ctx.capture_area_px.y1 <= st_output->mode.height_px);
}

void
request_video_capture_frame(
    struct capture_frame_context *frame_ctx,
    int32_t damage_x,
    int32_t damage_y,
    int32_t damage_w,
    int32_t damage_h
) {
    frame_ctx->frame = ext_image_copy_capture_session_v1_create_frame(
        frame_ctx->wl_capture_session
    );

    ext_image_copy_capture_frame_v1_attach_buffer(
        frame_ctx->frame,
        frame_ctx->st_buffer.wl_buffer
    );
    ext_image_copy_capture_frame_v1_damage_buffer(
        frame_ctx->frame,
        damage_x, damage_y, damage_w, damage_h
    );
    ext_image_copy_capture_frame_v1_add_listener(
        frame_ctx->frame,
        &image_copy_capture_frame_listener__video_capture,
        frame_ctx
    );
    ext_image_copy_capture_frame_v1_capture(
        frame_ctx->frame
    );
}


static inline bool
destroy_ffmpeg_audio(struct scran_output *st_output)
{
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;

    avcodec_free_context(&frame_ctx->av_codec_ctx_audio);
    av_frame_free(&frame_ctx->av_frame_captured_audio);
    av_audio_fifo_free(frame_ctx->av_audio_fifo);
    av_packet_free(&frame_ctx->av_packet_audio);

    return true;
}

static inline bool
init_ffmpeg_audio(struct scran_output *st_output)
{
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;

    // Float planar should be guaranteed supported for pipewire(?)
    // TODO: Retrieve the sample_fmt using avcodec_get_supported_config() if we
    // implement user-provided settings. FLTP/F32P is supported for AAC.
    static const enum AVSampleFormat sample_fmt     = AV_SAMPLE_FMT_FLTP;
    static const enum AVCodecID      codec_id       = AV_CODEC_ID_AAC;
    static const AVChannelLayout     channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    static const int                 sample_rate    = SCRAN_PIPEWIRE_SAMPLE_RATE;
    static const AVRational          time_base      = { 1, NSEC_PER_SEC };

    assert(channel_layout.nb_channels == SCRAN_PIPEWIRE_N_CHANNELS);

    if (!scran_pipewire_init(frame_ctx, ffmpeg_sample_format_to_pipewire(sample_fmt))) {
        return false;
    }

    // AVFrame (captured)
    frame_ctx->av_frame_captured_audio              = av_frame_alloc();
    frame_ctx->av_frame_captured_audio->format      = sample_fmt;
    frame_ctx->av_frame_captured_audio->sample_rate = sample_rate;
    frame_ctx->av_frame_captured_audio->ch_layout   = channel_layout;

    // AVCodec
    const AVCodec *codec = avcodec_find_encoder(codec_id);

    // AVCodecContext
    frame_ctx->av_codec_ctx_audio              = avcodec_alloc_context3(codec);
    if (frame_ctx->av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        frame_ctx->av_codec_ctx_audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    frame_ctx->av_codec_ctx_audio->sample_rate = sample_rate;
    frame_ctx->av_codec_ctx_audio->sample_fmt  = sample_fmt;
    frame_ctx->av_codec_ctx_audio->time_base   = time_base;
    frame_ctx->av_codec_ctx_audio->ch_layout   = channel_layout;
    avcodec_open2(frame_ctx->av_codec_ctx_audio, codec, NULL);

    // AVFrame (captured, cont.)
    frame_ctx->av_frame_captured_audio->nb_samples  = frame_ctx->av_codec_ctx_audio->frame_size;
    av_frame_get_buffer(frame_ctx->av_frame_captured_audio, 0);

    // AVAudioFifo
    frame_ctx->av_audio_fifo = av_audio_fifo_alloc(
        sample_fmt, channel_layout.nb_channels, frame_ctx->av_codec_ctx_audio->frame_size
    );

    // AVPacket (encoded)
    frame_ctx->av_packet_audio = av_packet_alloc();

    // AVStream
    AVStream *audio_stream = avformat_new_stream(frame_ctx->av_format_ctx, codec);
    assert(audio_stream == frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_AUDIO]);
    avcodec_parameters_from_context(audio_stream->codecpar, frame_ctx->av_codec_ctx_audio);

    return true;
}

// TODO:
//  - Error checking
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
    // TODO: Is output::mode framerate_mhz same as the capture framerate?
    const AVRational av_framerate_captured = { st_output->mode.refresh_rate_mHz, MILLIHZ_PER_HZ };
    // INFO: Using 1/NSEC_PER_SEC due to (wayland's) frame::presentation_time()
    // giving time with nanosecond precision.
    const AVRational av_time_base_captured = { 1, NSEC_PER_SEC };

    enum ScranAVTransposeDir av_transpose_direction =
        wl_output_transform_to_ffmpeg_transpose_dir__inverse(st_output->transform);

    const int width_px_to_encode = get_transformed_width(width_px_captured, height_px_captured, st_output->transform);
    const int height_px_to_encode = get_transformed_height(width_px_captured, height_px_captured, st_output->transform);
    // NOTE: Some pixel formats (and some file formats), e.g. YUV420P, require
    //       even-numbered (or some other multiplier) height and/or width.
    const enum AVPixelFormat av_pixel_format_to_encode = AV_PIX_FMT_YUV420P;


    // AVFrame (captured)
    frame_ctx->av_frame_captured              = av_frame_alloc();
    frame_ctx->av_frame_captured->width       = width_px_captured;
    frame_ctx->av_frame_captured->height      = height_px_captured;
    frame_ctx->av_frame_captured->format      = av_pixel_format_captured;
    // XXX: We won't ever get planar src frame buffers, right..?
    frame_ctx->av_frame_captured->linesize[0] = get_capture_stride(st_output);
    assert(frame_ctx->av_frame_captured->linesize[0] == frame_ctx->pixel_stride * frame_ctx->source_width_px);


    // AVFilter
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
           // XXX: libavfilter can't transpose by 180, unless with two
           // sequential transposes, or with a "rotate" filter.
           // SCRAN_AV_TRANSPOSE_DIR_180 is not actually a 180 transform.
           // We set file metadata instead.
           // TODO: Implement yuv conversion in our simd code so that we can
           // transpose there for video as well. Should end up significantly
           // more efficient than libavfilter, even for 90-degree transforms
           // TODO: Also reconsider simply doing metadata-based rotation by
           // default for video.
        || av_transpose_direction == SCRAN_AV_TRANSPOSE_DIR_180
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

    // AVFilter: Sink (writes into av_frame_to_encode)
    frame_ctx->av_filter_buffersink_ctx = avfilter_graph_alloc_filter(
            frame_ctx->av_filter_graph, avfilter_get_by_name("buffersink"), "out"
    );
#if !defined LIBAVUTIL_VERSION_INT || (LIBAVUTIL_VERSION_INT < AV_VERSION_INT(59,36,100))
    enum AVPixelFormat _pix_fmts[] = { av_pixel_format_to_encode, AV_PIX_FMT_NONE };
    av_opt_set_int_list( frame_ctx->av_filter_buffersink_ctx,
            "pix_fmts", _pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN
    );
#else
    av_opt_set_array( frame_ctx->av_filter_buffersink_ctx,
            "pixel_formats", AV_OPT_SEARCH_CHILDREN,
            0, 1, AV_OPT_TYPE_PIXEL_FMT, &av_pixel_format_to_encode
    );
#endif /* LIBAVUTIL_VERSION_INT */
    avfilter_init_dict(frame_ctx->av_filter_buffersink_ctx, NULL);

    avfilter_link(_sink_input_filter, 0,
                  frame_ctx->av_filter_buffersink_ctx, 0);

    avfilter_graph_config(frame_ctx->av_filter_graph, NULL);


    // AVFrame (converted, ready to be fed to encoder)
    frame_ctx->av_frame_to_encode         = av_frame_alloc();
    frame_ctx->av_frame_to_encode->width  = width_px_to_encode;
    frame_ctx->av_frame_to_encode->height = height_px_to_encode;
    frame_ctx->av_frame_to_encode->format = av_pixel_format_to_encode;


    // AVCodec
    static const char *codec_fallbacks[] = {
        "libx264",  // requires GPL ffmpeg build
        // TODO(libopenh264):
        // - Why does it have such bad performance (high cpu usage,
        //   stutters) compared to libx264?
        // - Why is it just giving green frames on Fedora? (works on NixOS.)
        //   - Packet sizes are all very small. First is much smaller than
        //     normal, and subsequent ones are almost all at minimum size (14),
        //     despite a lot of movement in the capture frame.
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


    // AVFormat
    const char *output_filepath = NULL;
    if (g_state.options.output_to_stdout) {
        output_filepath = "pipe:1";
    } else {
        static const char _ext_mp4[SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX] =
            _FORMAT_MP4_FILE_EXTENSION;
        output_filepath =
            scran_update_output_filepath(&g_state.options, _ext_mp4);
    }
    avformat_alloc_output_context2(&frame_ctx->av_format_ctx, NULL, _FORMAT_MP4_NAME, output_filepath);


    // AVCodecContext (encoder)
    frame_ctx->av_codec_ctx = avcodec_alloc_context3(codec);
    if (frame_ctx->av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        frame_ctx->av_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    // -- Values tied to encoder input/environment --
    frame_ctx->av_codec_ctx->width     = frame_ctx->av_frame_to_encode->width;
    frame_ctx->av_codec_ctx->height    = frame_ctx->av_frame_to_encode->height;
    frame_ctx->av_codec_ctx->pix_fmt   = frame_ctx->av_frame_to_encode->format;
    frame_ctx->av_codec_ctx->framerate = av_framerate_captured;
    frame_ctx->av_codec_ctx->time_base = av_time_base_captured;
    // -- Values to be tweaked (may depend on encoder/format) --
    // NOTE: B-frames not be supported by all codecs (e.g. libopenh264?)
    frame_ctx->av_codec_ctx->max_b_frames = 2;
    frame_ctx->av_codec_ctx->gop_size     = 120;
    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "crf"   , "20"       , 0);
    av_dict_set(&codec_opts, "preset", "superfast", 0);
    avcodec_open2(frame_ctx->av_codec_ctx, codec, &codec_opts);
    av_dict_free(&codec_opts);

    assert(frame_ctx->av_frame_to_encode->width  != 0);
    assert(frame_ctx->av_frame_to_encode->height != 0);


    // AVPacket (encoded)
    frame_ctx->av_packet = av_packet_alloc();


    // AVStream
    AVStream *_av_stream = avformat_new_stream(frame_ctx->av_format_ctx, codec);
    assert(_av_stream == frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO]);
    // NOTE: Requested time_base. Final time_base will have been selected by
    // libav after write_header. av_packet_rescale_ts() exists to convert from
    // Encoder timestamps to Format timestamps prior to handing off the packet.
    assert(frame_ctx->av_codec_ctx->framerate.num != 0);
    assert(frame_ctx->av_codec_ctx->framerate.den != 0);
    _av_stream->time_base = av_inv_q(frame_ctx->av_codec_ctx->framerate);
    avcodec_parameters_from_context(_av_stream->codecpar, frame_ctx->av_codec_ctx);

    // XXX: Hotfix for ffmpeg's lack of 180 rotation. See comment above in the
    // avfilter init section.
    if (av_transpose_direction == SCRAN_AV_TRANSPOSE_DIR_180) {
        AVPacketSideData *_side_data = av_packet_side_data_new(
            &_av_stream->codecpar->coded_side_data,
            &_av_stream->codecpar->nb_coded_side_data,
            AV_PKT_DATA_DISPLAYMATRIX,
            sizeof(int32_t) * 9,
            0
        );
        av_display_rotation_set((int32_t *)_side_data->data, 180.0);
    }


    if (!g_state.options.disable_audio_capture && !frame_ctx->audio_disable_modifier_active) {
        if (init_ffmpeg_audio(st_output)) {
            frame_ctx->audio_active = true;
        } else {
            eprintf("WARNING: Failed to init audio capture.\n");
            scran_pipewire_reset();
            destroy_ffmpeg_audio(st_output);
        }
    }


    // AVFormat (cont.)
    avio_open(&(frame_ctx->av_format_ctx)->pb, output_filepath, AVIO_FLAG_WRITE);
    assert(!((frame_ctx->av_format_ctx)->oformat->flags & AVFMT_NOFILE));
    AVDictionary *format_opts = NULL;
#if !defined LIBAVFORMAT_VERSION_INT || (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(61,4,100))
    // Best we can do for seeking/playback-duration compatibility without remuxing or custom
    // "hybrid mp4" implementation, if hybrid_fragmented is unavailable.
    av_dict_set(&format_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
#else
    av_dict_set(&format_opts, "movflags", "hybrid_fragmented", 0);
#endif /* LIBAVFORMAT_VERSION_INT */
    int format_ret = avformat_write_header(frame_ctx->av_format_ctx, &format_opts);
    av_dict_free(&format_opts);
    if (format_ret < 0) {
        eprintf("Failed to write file header (filepath: %s)\n", output_filepath);
        destroy_ffmpeg(st_output); // TODO: goto fail?
        return false;
    }

    return true;
}

void
destroy_ffmpeg(struct scran_output *st_output)
{
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;

    // Note: Most (all?) of these are fine to call with null pointers, despite
    // the asserts
    avio_close(frame_ctx->av_format_ctx->pb);
    av_packet_free(&frame_ctx->av_packet);
    avcodec_free_context(&frame_ctx->av_codec_ctx);
    // Freeing the format context frees the linked stream for us.
    avformat_free_context(frame_ctx->av_format_ctx);
    av_frame_free(&frame_ctx->av_frame_to_encode);
    // Freeing the graph frees any linked filter contexts for us.
    avfilter_graph_free(&frame_ctx->av_filter_graph);
    av_frame_free(&frame_ctx->av_frame_captured);
}


bool
request_video_capture(struct scran_output *st_output)
{
    // TODO: Assert instead?
    if (st_output->capture.frame_ctx.capturing_video) {
        DEBUG("Already capturing...\n");
        return false;
    }

    if (!set_selection_freeze_size(st_output)) {
        eprintf("Can't start video capture without frozen selection size.\n");
        return false;
    }

    assert(( st_output->selection_ctx.selection_state == SELECTION_COMPLETE_FREEZE_SIZE
             || st_output->selection_ctx.selection_state == SELECTION_REBASING_FREEZE_SIZE)
           && st_output->selection_ctx.box_px.x1
           && st_output->selection_ctx.box_px.y1
           // TODO: Assert box is within output dimensions
           // TODO: Assert box is not inverted
    );

    if (!init_ffmpeg(st_output)) {
        eprintf("Error: Failed to initialize ffmpeg libraries.\n");
        unset_selection_freeze_size(st_output); // TODO: goto fail?
        return false;
    }

    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_keymap_item_set_disabled(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_DISABLE_REASON_CAPTURING_VIDEO, true);
        scran_ui_keymap_item_set_color(   ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_KEYMAP_COLOR_VIDEO_CAPTURE);
        scran_ui_keymap_item_set_locked(  ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, true);
    }
    set_selection_surface_theme(st_output, SURFACE_THEME_VIDEO_CAPTURE);
    request_selection_surface_frame_callback(st_output);

    // image-copy-capture protocol guarantees we get presentation time based
    // on system monotonic time.
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // XXX: Will overflow at tv_sec > ~584.9 years...
    st_output->capture.frame_ctx.presentation_time_nsec_start = ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;

    // Get initial frame. Subsequent capture requests happen within
    // frame::ready, similar to the wl_surface callback event loop
    request_video_capture_frame(
        &st_output->capture.frame_ctx,
        // Ensure the first frame is fully rendered
        0, 0, st_output->mode.width_px, st_output->mode.height_px
    );

    if (st_output->capture.frame_ctx.audio_active) {
        scran_pipewire_connect();
    }

    st_output->capture.frame_ctx.capturing_video = true;
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    return true;
}

void
request_end_video_capture(struct scran_output *st_output)
{
    // Ensure one last frame is triggered as soon as possible, even if
    // no damage has been reported by the compositor. This ensures
    // variable framerate recordings will end at an appropriate
    // timestamp. This also lets the frame listener finalize the
    // recording and clean up as soon as possible.
    ext_image_copy_capture_frame_v1_destroy(st_output->capture.frame_ctx.frame);

    // XXX TODO: Client-requested damage does *not* necessarily trigger a new
    // ::ready from the compositor, if no actual damage has occurred. Sway, for
    // example, ignores it, and forces us to wait for an indefinite amount of
    // time for the capture to end, if no movement has happened on screen.
    //
    // The optimal solution might be to handle this internally anyways, either
    // manually duplicating it or changing the frame duration retroactively.
    //
    // HACK: For now, just force a surface redraw to effectively force the
    // compositor to send us another capture frame... (May not work if only
    // the transparent part of the selection surface is visible, e.g. with
    // "fullscreen" capture, but seems effective even then on my local
    // Sway (v1.11).)
    set_force_redraw_selection_surface_buffers(st_output);
    request_selection_surface_frame_callback(st_output);

    request_video_capture_frame(
        &st_output->capture.frame_ctx,
        0, 0, st_output->mode.width_px, st_output->mode.height_px
    );
    st_output->capture.frame_ctx.capturing_video = false;
}

// Should only be called once the video capture event loop is finished.
//    Call request_end_video_capture() instead to initiate graceful completion.
// TODO: Rename this function to be less ambiguous now that we have audio as well.
void
end_video_capture(struct scran_output *st_output)
{
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;

    if (frame_ctx->audio_active) {
        scran_pipewire_reset();

        // Drain audio codec
        avcodec_send_frame(frame_ctx->av_codec_ctx_audio, NULL);
        assert(frame_ctx->av_packet_audio != NULL);
        while (avcodec_receive_packet(frame_ctx->av_codec_ctx_audio, frame_ctx->av_packet_audio) != AVERROR_EOF) {
            write_audio_packet(frame_ctx, frame_ctx->av_packet_audio);
        }

        destroy_ffmpeg_audio(st_output);

        frame_ctx->audio_active = false;
    }

    // Drain video codec
    avcodec_send_frame(frame_ctx->av_codec_ctx, NULL);
    assert(frame_ctx->av_packet != NULL);
    while (avcodec_receive_packet(frame_ctx->av_codec_ctx, frame_ctx->av_packet) != AVERROR_EOF) {
        write_video_frame(frame_ctx, frame_ctx->av_packet);
    }

    av_write_trailer(frame_ctx->av_format_ctx);
    eprintf("Video saved: %s\n", g_state.options.output_path);

    const char *output_path = g_state.options.output_to_stdout ? NULL : frame_ctx->av_format_ctx->url;
    update_clipboard(&g_state.seat.datacontrol, NULL, NULL, output_path);
    if (output_path != NULL) {
        scran_portal_notify_file_saved(output_path);
    }

    destroy_ffmpeg(st_output);

    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_keymap_item_set_disabled(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_DISABLE_REASON_CAPTURING_VIDEO, false);
        scran_ui_keymap_item_set_color(   ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_KEYMAP_COLOR_DEFAULT);
        scran_ui_keymap_item_set_locked(  ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, false);
    }
    set_selection_surface_theme(st_output, SURFACE_THEME_DEFAULT);
    request_selection_surface_frame_callback(st_output);

    unset_selection_freeze_size(st_output);

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


void
dispatch_image_capture_event(struct scran_output *st_output)
{
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            st_output->capture.frame_ctx.wl_capture_session
        );
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener__image_capture, st_output);
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        st_output->capture.frame_ctx.st_buffer.wl_buffer
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}


static inline void
_print_slurp_string(struct scran_output *st_output)
{
    const double scale = st_output->selection_surface.surface.final_scale_factor_normalized;
    const struct scran_output_xdg_geometry geometry = st_output->xdg_geometry;
    const struct BLBoxI box_px = st_output->selection_ctx.box_px;

    assert(!blboxi_is_inverted(box_px));
    const struct BLRectI rect_logical = {
        .x = round(  box_px.x0              / scale),
        .y = round(  box_px.y0              / scale),
        .w = round( (box_px.x1 - box_px.x0) / scale),
        .h = round( (box_px.y1 - box_px.y0) / scale),
    };

    // TODO: Assert nothing else was sent to stdout?
    fprintf(stdout, "%d,%d %dx%d\n",
            geometry.x_logical + rect_logical.x,
            geometry.y_logical + rect_logical.y,
            rect_logical.w,
            rect_logical.h
    );
    fflush(stdout);
}

bool
request_image_capture(struct scran_output *st_output)
{
    // See TODO at call site
    assert(!st_output->capture.frame_ctx.capturing_video);

    if (g_state.options.produce_slurp) {
        _print_slurp_string(st_output);
        return true;
    }

    dispatch_image_capture_event(st_output);
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    return true;
}

