#include "capture.h"
#include "options.h"
#include "pipewires.h"
#include "state-util.h"
#include "util/lib-interop.h"


// XXX: Seems like the underlying FFOutputFormat structs aren't exposed in the
// public API (e.g. ff_mp4_muxer etc.), so we must let libavformat run its
// "guessing"/scoring algorithm using a format-name string.
#define FFMPEG_FORMAT_MP4_NAME "mp4"


static bool
init_ffmpeg_audio(struct scran_output *st_output)
{
    struct ffmpeg_context *ffmpeg_ctx = &st_output->capture.ffmpeg_ctx;

    // Float planar should be guaranteed supported for pipewire(?)
    // TODO: Retrieve the sample_fmt using avcodec_get_supported_config() if we
    // implement user-provided settings. FLTP/F32P is supported for AAC.
    static const enum AVSampleFormat sample_fmt     = AV_SAMPLE_FMT_FLTP;
    static const enum AVCodecID      codec_id       = AV_CODEC_ID_AAC;
    static const AVChannelLayout     channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    static const int                 sample_rate    = SCRAN_PIPEWIRE_SAMPLE_RATE;
    static const AVRational          time_base      = { 1, NSEC_PER_SEC };

    assert(channel_layout.nb_channels == SCRAN_PIPEWIRE_N_CHANNELS);

    if (!scran_pipewire_init(st_output, ffmpeg_sample_format_to_pipewire(sample_fmt))) {
        return false;
    }

    // AVFrame (captured)
    ffmpeg_ctx->av_frame_captured_audio              = av_frame_alloc();
    ffmpeg_ctx->av_frame_captured_audio->format      = sample_fmt;
    ffmpeg_ctx->av_frame_captured_audio->sample_rate = sample_rate;
    ffmpeg_ctx->av_frame_captured_audio->ch_layout   = channel_layout;

    // AVCodec
    const AVCodec *codec = avcodec_find_encoder(codec_id);

    // AVCodecContext
    ffmpeg_ctx->av_codec_ctx_audio              = avcodec_alloc_context3(codec);
    if (ffmpeg_ctx->av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        ffmpeg_ctx->av_codec_ctx_audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    ffmpeg_ctx->av_codec_ctx_audio->sample_rate = sample_rate;
    ffmpeg_ctx->av_codec_ctx_audio->sample_fmt  = sample_fmt;
    ffmpeg_ctx->av_codec_ctx_audio->time_base   = time_base;
    ffmpeg_ctx->av_codec_ctx_audio->ch_layout   = channel_layout;
    avcodec_open2(ffmpeg_ctx->av_codec_ctx_audio, codec, NULL);

    // AVFrame (captured, cont.)
    ffmpeg_ctx->av_frame_captured_audio->nb_samples  = ffmpeg_ctx->av_codec_ctx_audio->frame_size;
    av_frame_get_buffer(ffmpeg_ctx->av_frame_captured_audio, 0);

    // AVAudioFifo
    ffmpeg_ctx->av_audio_fifo = av_audio_fifo_alloc(
        sample_fmt, channel_layout.nb_channels, ffmpeg_ctx->av_codec_ctx_audio->frame_size
    );

    // AVPacket (encoded)
    ffmpeg_ctx->av_packet_audio = av_packet_alloc();

    // AVStream
    AVStream *audio_stream = avformat_new_stream(ffmpeg_ctx->av_format_ctx, codec);
    assert(audio_stream == ffmpeg_ctx->av_format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_AUDIO]);
    avcodec_parameters_from_context(audio_stream->codecpar, ffmpeg_ctx->av_codec_ctx_audio);

    return true;
}

static bool
destroy_ffmpeg_audio(struct scran_output *st_output)
{
    struct ffmpeg_context *ffmpeg_ctx = &st_output->capture.ffmpeg_ctx;

    avcodec_free_context(&ffmpeg_ctx->av_codec_ctx_audio);
    av_frame_free(&ffmpeg_ctx->av_frame_captured_audio);
    av_audio_fifo_free(ffmpeg_ctx->av_audio_fifo);
    av_packet_free(&ffmpeg_ctx->av_packet_audio);

    return true;
}

static void
destroy_ffmpeg_video(struct scran_output *st_output)
{
    struct ffmpeg_context *ffmpeg_ctx = &st_output->capture.ffmpeg_ctx;

    // Note: Most (all?) of these are fine to call with null pointers, despite
    // the asserts
    avio_close(ffmpeg_ctx->av_format_ctx->pb);
    av_packet_free(&ffmpeg_ctx->av_packet);
    avcodec_free_context(&ffmpeg_ctx->av_codec_ctx);
    // Freeing the format context frees the linked stream for us.
    avformat_free_context(ffmpeg_ctx->av_format_ctx);
    av_frame_free(&ffmpeg_ctx->av_frame_to_encode);
}

// TODO:
//  - Error checking
//  - Encoding parameters:
//      - Let user override the encoding parameters
//      - Decide on good defaults
//      - Store defaults in a const struct (or whatever format libav prefers)
//
//  - Don't pass scran_output. Just width/height etc.
//
static bool
init_ffmpeg(struct scran_output *st_output, const BLPointI dimensions)
{
    struct scran_output_capture *capture    = &st_output->capture;
    struct ffmpeg_context       *ffmpeg_ctx = &capture->ffmpeg_ctx;

    // TODO: Is output::mode framerate_mhz same as the capture framerate?
    const AVRational av_framerate_captured = { st_output->mode.refresh_rate_mHz, MILLIHZ_PER_HZ };
    // INFO: Using 1/NSEC_PER_SEC due to (wayland's) frame::presentation_time()
    // giving time with nanosecond precision.
    const AVRational av_time_base_captured = { 1, NSEC_PER_SEC };

    // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be
    // divisible by 2. TODO: Also update selection area visuals to this width.
    const int width_px_to_encode  = dimensions.x & ~0b1;
    const int height_px_to_encode = dimensions.y & ~0b1;
    // NOTE: Some pixel formats (and some file formats), e.g. YUV420P, require
    //       even-numbered (or some other multiplier) height and/or width.
    const enum AVPixelFormat av_pixel_format_to_encode = AV_PIX_FMT_YUV420P;


    // AVFrame (converted, ready to be fed to encoder)
    ffmpeg_ctx->av_frame_to_encode                  = av_frame_alloc();
    ffmpeg_ctx->av_frame_to_encode->width           = width_px_to_encode;
    ffmpeg_ctx->av_frame_to_encode->height          = height_px_to_encode;
    ffmpeg_ctx->av_frame_to_encode->format          = av_pixel_format_to_encode;
    // scranrot outputs full-range BT.709 Y'CbCr. The capture source is desktop
    // RGB, so we use BT.709 video metadata for broad playback compatibility.
    ffmpeg_ctx->av_frame_to_encode->color_range     = AVCOL_RANGE_JPEG;
    ffmpeg_ctx->av_frame_to_encode->colorspace      = AVCOL_SPC_BT709;
    ffmpeg_ctx->av_frame_to_encode->color_primaries = AVCOL_PRI_BT709;
    ffmpeg_ctx->av_frame_to_encode->color_trc       = AVCOL_TRC_BT709;


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
        assert(scran_stdout_check_reservation(&st_output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO));
        output_filepath = "pipe:1";
    } else {
        static const char mp4_file_extension[SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX] = ".mp4";
        output_filepath = scran_update_output_filepath(&g_state.options, mp4_file_extension);
    }
    avformat_alloc_output_context2(&ffmpeg_ctx->av_format_ctx, NULL, FFMPEG_FORMAT_MP4_NAME, output_filepath);


    // AVCodecContext (encoder)
    ffmpeg_ctx->av_codec_ctx = avcodec_alloc_context3(codec);
    if (ffmpeg_ctx->av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        ffmpeg_ctx->av_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    // -- Values tied to encoder input/environment --
    ffmpeg_ctx->av_codec_ctx->width           = ffmpeg_ctx->av_frame_to_encode->width;
    ffmpeg_ctx->av_codec_ctx->height          = ffmpeg_ctx->av_frame_to_encode->height;
    ffmpeg_ctx->av_codec_ctx->pix_fmt         = ffmpeg_ctx->av_frame_to_encode->format;
    ffmpeg_ctx->av_codec_ctx->color_range     = ffmpeg_ctx->av_frame_to_encode->color_range;
    ffmpeg_ctx->av_codec_ctx->colorspace      = ffmpeg_ctx->av_frame_to_encode->colorspace;
    ffmpeg_ctx->av_codec_ctx->color_primaries = ffmpeg_ctx->av_frame_to_encode->color_primaries;
    ffmpeg_ctx->av_codec_ctx->color_trc       = ffmpeg_ctx->av_frame_to_encode->color_trc;
    ffmpeg_ctx->av_codec_ctx->framerate       = av_framerate_captured;
    ffmpeg_ctx->av_codec_ctx->time_base       = av_time_base_captured;
    // -- Values to be tweaked (may depend on encoder/format) --
    // NOTE: B-frames not be supported by all codecs (e.g. libopenh264?)
    ffmpeg_ctx->av_codec_ctx->max_b_frames    = 2;
    ffmpeg_ctx->av_codec_ctx->gop_size        = 120;
    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "crf"   , "20"       , 0);
    av_dict_set(&codec_opts, "preset", "superfast", 0);
    avcodec_open2(ffmpeg_ctx->av_codec_ctx, codec, &codec_opts);
    av_dict_free(&codec_opts);

    assert(ffmpeg_ctx->av_frame_to_encode->width  != 0);
    assert(ffmpeg_ctx->av_frame_to_encode->height != 0);


    // AVPacket (encoded)
    ffmpeg_ctx->av_packet = av_packet_alloc();


    // AVStream
    AVStream *_av_stream = avformat_new_stream(ffmpeg_ctx->av_format_ctx, codec);
    assert(_av_stream == ffmpeg_ctx->av_format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_VIDEO]);
    // NOTE: Requested time_base. Final time_base will have been selected by
    // libav after write_header. av_packet_rescale_ts() exists to convert from
    // Encoder timestamps to Format timestamps prior to handing off the packet.
    assert(ffmpeg_ctx->av_codec_ctx->framerate.num != 0);
    assert(ffmpeg_ctx->av_codec_ctx->framerate.den != 0);
    _av_stream->time_base = av_inv_q(ffmpeg_ctx->av_codec_ctx->framerate);
    avcodec_parameters_from_context(_av_stream->codecpar, ffmpeg_ctx->av_codec_ctx);


    if (!g_state.options.disable_audio_capture && !capture->audio_disable_modifier_active) {
        if (init_ffmpeg_audio(st_output)) {
            capture->audio_active = true;
        } else {
            eprintf("WARNING: Failed to init audio capture.\n");
            scran_pipewire_reset();
            destroy_ffmpeg_audio(st_output);
        }
    }


    // AVFormat (cont.)
    avio_open(&(ffmpeg_ctx->av_format_ctx)->pb, output_filepath, AVIO_FLAG_WRITE);
    assert(!((ffmpeg_ctx->av_format_ctx)->oformat->flags & AVFMT_NOFILE));
    AVDictionary *format_opts = NULL;
#if !defined LIBAVFORMAT_VERSION_INT || (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(61,4,100))
    // Best we can do for seeking/playback-duration compatibility without remuxing or custom
    // "hybrid mp4" implementation, if hybrid_fragmented is unavailable.
    av_dict_set(&format_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
#else
    av_dict_set(&format_opts, "movflags", "hybrid_fragmented", 0);
#endif /* LIBAVFORMAT_VERSION_INT */
    int format_ret = avformat_write_header(ffmpeg_ctx->av_format_ctx, &format_opts);
    av_dict_free(&format_opts);
    if (format_ret < 0) {
        eprintf("Failed to write file header (filepath: %s)\n", output_filepath);
        destroy_ffmpeg_video(st_output); // TODO: goto fail?
        return false;
    }

    return true;
}

// Can be used for both audio and video encoders
bool
capture_video_drain_writer(
    struct scran_output *st_output,
    AVCodecContext *codec_ctx,
    AVPacket *packet,
    capture_video_write_packet_fn write_packet_fn,
    const char *stream_name
) {
    assert(packet != NULL);

    int ret = avcodec_send_frame(codec_ctx, NULL);
    if (ret == AVERROR_EOF) {
        return true;
    }
    if (ret < 0) {
        eprintf("Error: Failed to start draining %s encoder (%d).\n", stream_name, ret);
        return false;
    }

    for (;;) {
        ret = avcodec_receive_packet(codec_ctx, packet);
        if (ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            eprintf("Error: Failed while draining %s encoder (%d).\n", stream_name, ret);
            return false;
        }

        write_packet_fn(st_output, packet);
    }
}


bool
capture_video_init_writers(struct scran_output *st_output, BLPointI dimensions)
{
    return init_ffmpeg(st_output, dimensions);
}

void
capture_video_destroy_video_writer(struct scran_output *st_output)
{
    destroy_ffmpeg_video(st_output);
}

void
capture_video_destroy_audio_writer(struct scran_output *st_output)
{
    destroy_ffmpeg_audio(st_output);
}

void
capture_video_write_video_packet(
    struct scran_output *output,
    AVPacket *pkt // Encoded frame
) {
    struct ffmpeg_context *ffmpeg_ctx = &output->capture.ffmpeg_ctx;

    assert(pkt != NULL);

    const AVStream *const _av_stream = ffmpeg_ctx->av_format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_VIDEO];

    pkt->stream_index = SCRAN_AV_FORMAT_STREAM_IDX_VIDEO;
    assert(pkt->stream_index == _av_stream->index);

    // NOTE: This is doing the work of av_packet_rescale_ts(), but with
    // asserts instead of conditionals. Just switch to that or to
    // if-statements if indeterminism becomes necessary.
    assert(pkt->pts != AV_NOPTS_VALUE);
    pkt->pts = av_rescale_q(pkt->pts, ffmpeg_ctx->av_codec_ctx->time_base, _av_stream->time_base);
    assert(pkt->dts != AV_NOPTS_VALUE);
    pkt->dts = av_rescale_q(pkt->dts, ffmpeg_ctx->av_codec_ctx->time_base, _av_stream->time_base);

    // We don't use durations for VFR.
    //     XXX: This gives a warning on the last frame before
    //     avformat_write_header(). Not sure if worth fixing.
    assert(pkt->duration <= 0);

    av_interleaved_write_frame(ffmpeg_ctx->av_format_ctx, pkt);
}

void
capture_video_write_audio_packet(
    struct scran_output *st_output,
    AVPacket *pkt // Encoded frame
) {
    struct ffmpeg_context *ffmpeg_ctx = &st_output->capture.ffmpeg_ctx;

    assert(pkt != NULL);

    const AVStream *const av_stream = ffmpeg_ctx->av_format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_AUDIO];
    pkt->stream_index = SCRAN_AV_FORMAT_STREAM_IDX_AUDIO;
    assert(pkt->stream_index == av_stream->index);

    // NOTE: This is doing the work of av_packet_rescale_ts(), but with
    // asserts instead of conditionals. Just switch to that or to
    // if-statements if indeterminism becomes necessary.
    assert(pkt->pts != AV_NOPTS_VALUE);
    pkt->pts = av_rescale_q(pkt->pts, ffmpeg_ctx->av_codec_ctx_audio->time_base, av_stream->time_base);
    assert(pkt->dts != AV_NOPTS_VALUE);
    pkt->dts = av_rescale_q(pkt->dts, ffmpeg_ctx->av_codec_ctx_audio->time_base, av_stream->time_base);
    assert(pkt->duration != AV_NOPTS_VALUE);
    pkt->duration = av_rescale_q(pkt->duration, ffmpeg_ctx->av_codec_ctx_audio->time_base, av_stream->time_base);

    av_interleaved_write_frame(ffmpeg_ctx->av_format_ctx, pkt);
}

bool
capture_video_write_video_frame(
    struct scran_output *output,
    struct capture_frame_context *frame_ctx,
    const struct capture_session_context *session,
    const struct capture_buffer_area_context *buffer_area_ctx
) {
    struct ffmpeg_context *ffmpeg = &output->capture.ffmpeg_ctx;

    // Crop and convert
    {
        // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be divisible by 2.
        // XXX TODO: Collect this bit zeroing logic somehow? (Duplicated in init_ffmpeg.)
        const int area_w_px = blboxi_width_abs_unsafe(buffer_area_ctx->area_px) & ~0b1;
        const int area_h_px = blboxi_height_abs_unsafe(buffer_area_ctx->area_px) & ~0b1;

        uint32_t rgba32_shuffle = wl_shm_format_to_scranrot_yuv_rgba32_shuffle(session->shm_format);
        if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
            eprintf(
                "WARNING: Output's pixel format (%x) not recognized. Please report this as a bug. Attempting anyways...\n",
                session->shm_format
            );
            rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
        }

        // XXX: Scranrot does not support flipped transforms yet, so we just
        // record it flipped for now, rather than blocking capture entirely.
        enum wl_output_transform transform = wl_output_transform_without_flip(frame_ctx->source_transform);

        AVFrame *frame = ffmpeg->av_frame_to_encode;
        void *const frame_buffer = output->capture.img_data_2;

        assert(frame->width == get_transformed_width(area_w_px, area_h_px, transform));
        assert(frame->height == get_transformed_height(area_w_px, area_h_px, transform));

        if (!scranrot_transform_framebuffer_to_yuv420(
                buffer_area_ctx->area_start_address,
                area_w_px,
                area_h_px,
                buffer_area_ctx->source_row_bytes,
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
        frame->pts = frame_ctx->presentation_time_nsec - output->capture.video_presentation_time_nsec_start;
    }

    // Encode
    int _ret_enc = avcodec_send_frame(ffmpeg->av_codec_ctx, ffmpeg->av_frame_to_encode);
    assert(_ret_enc != AVERROR(EINVAL));
    while (_ret_enc >= 0) {
        _ret_enc = avcodec_receive_packet(ffmpeg->av_codec_ctx, ffmpeg->av_packet);
        assert(_ret_enc != AVERROR(EINVAL));

        if (_ret_enc == AVERROR_EOF || _ret_enc == AVERROR(EAGAIN)) {
            break;
        } else if (_ret_enc < 0) {
            eprintf("Error while encoding frame\n");
            return false;
        }

        capture_video_write_video_packet(output, ffmpeg->av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    av_packet_unref(ffmpeg->av_packet);
    return true;
}
