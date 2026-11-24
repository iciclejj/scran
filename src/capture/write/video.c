#include "capture.h"
#include "dbus.h"
#include "options.h"
#include "pipewires.h"
#include "state-util.h"
#include "util/lib-interop.h"
#include "util/util.h"

#include <limits.h>
#include <libavutil/opt.h>


struct encoder_info {
    const char *encoder;
    const char *codec;
};

// In order of priority
enum video_encoder_index {
    // Requires GPL ffmpeg build
    VIDEO_ENCODER_LIBX264,
    // TODO: Why does it have such bad performance (high cpu usage, stutters) compared to libx264?
    VIDEO_ENCODER_LIBOPENH264,
    VIDEO_ENCODER_LIBVPX_VP9,
    VIDEO_ENCODER_LIBSVTAV1,
};
static const struct encoder_info video_encoders[] = {
    [VIDEO_ENCODER_LIBX264]     = { .encoder = "libx264",     .codec = "h264" },
    [VIDEO_ENCODER_LIBOPENH264] = { .encoder = "libopenh264", .codec = "h264" },
    [VIDEO_ENCODER_LIBVPX_VP9]  = { .encoder = "libvpx-vp9",  .codec = "vp9" },
    [VIDEO_ENCODER_LIBSVTAV1]   = { .encoder = "libsvtav1",   .codec = "av1" },
};

// In order of priority
enum audio_encoder_index {
    AUDIO_ENCODER_AAC,
    AUDIO_ENCODER_LIBOPUS,
};
static const struct encoder_info audio_encoders[] = {
    [AUDIO_ENCODER_AAC]     = { .encoder = "aac",     .codec = "aac" },
    [AUDIO_ENCODER_LIBOPUS] = { .encoder = "libopus", .codec = "opus" },
};

struct format_info {
    const char *muxer_name;
    const char *alias;
};
enum format_index {
    VIDEO_FORMAT_MP4,
    VIDEO_FORMAT_MKV,
};
static const struct format_info video_formats[] = {
    [VIDEO_FORMAT_MP4] = { .muxer_name = "mp4",      .alias = "mp4" },
    [VIDEO_FORMAT_MKV] = { .muxer_name = "matroska", .alias = "mkv" },
};

// TODO: reference this in the docs for viewing defaults.
// TODO: Take codec opts
static struct video_config {
    enum format_index format;
    const AVOutputFormat *oformat;
    uint32_t video_encoders;
    uint32_t audio_encoders;
} m_video_config = {
    .format         = VIDEO_FORMAT_MP4,
    // Default to trying all our encoders
    .video_encoders = UINT32_MAX >> (32 - ARRAY_LEN(video_encoders)),
    .audio_encoders = UINT32_MAX >> (32 - ARRAY_LEN(audio_encoders)),
};
_Static_assert(ARRAY_LEN(video_encoders) <= sizeof(m_video_config.video_encoders) * CHAR_BIT,
               "video encoder mask too small");
_Static_assert(ARRAY_LEN(audio_encoders) <= sizeof(m_video_config.audio_encoders) * CHAR_BIT,
               "audio encoder mask too small");

bool
capture_video_set_format(const char *name)
{
    for (size_t i = 0; i < ARRAY_LEN(video_formats); ++i) {
        if (!strcmp(name, video_formats[i].alias) || !strcmp(name, video_formats[i].muxer_name)) {
            m_video_config.format = i;
            return true;
        }
    }
    eprintf("Error: Unknown video format '%s'. Available formats:", name);
    for (size_t i = 0; i < ARRAY_LEN(video_formats); ++i) {
        eprintf("%s%s (%s)", i ? ", " : " ", video_formats[i].alias, video_formats[i].muxer_name);
    }
    eprintf(".\n");
    return false;
}

static bool
set_encoder_mask(uint32_t *mask, const struct encoder_info *encoders, size_t count, const char *name, const char *stream_name)
{
    uint32_t selected = 0;

    // Prioritize codec, since names can overlap, e.g. aac/aac
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(name, encoders[i].codec)) {
            selected |= 1U << i;
        }
    }
    if (!selected) {
        for (size_t i = 0; i < count; ++i) {
            if (!strcmp(name, encoders[i].encoder)) {
                selected = 1U << i;
                break;
            }
        }
    }

    if (!selected) {
        eprintf("Error: Unknown %s codec or encoder '%s'. Available encoders (codec):", stream_name, name);
        for (size_t i = 0; i < count; ++i) {
            eprintf("%s%s (%s)", i ? ", " : " ", encoders[i].encoder, encoders[i].codec);
        }
        eprintf(".\n");
        return false;
    }

    *mask = selected;
    return true;
}

bool
capture_video_set_video_encoder(const char *name)
{
    return set_encoder_mask(&m_video_config.video_encoders, video_encoders, ARRAY_LEN(video_encoders), name, "video");
}

bool
capture_video_set_audio_encoder(const char *name)
{
    return set_encoder_mask(&m_video_config.audio_encoders, audio_encoders, ARRAY_LEN(audio_encoders), name, "audio");
}


static void
print_unusable_encoders(const char *stream_name, const struct encoder_info *encoders, size_t count, uint32_t mask)
{
    eprintf("Error: No usable %s encoder found. Attempted encoders:", stream_name);
    for (size_t i = 0; i < count; ++i) {
        if (mask & (1U << i)) {
            eprintf(" %s", encoders[i].encoder);
        }
    }
    eprintf(".\n");
}

// Safely handles NULL pointers, and ensures they're all NULL when done.
static void
destroy_ffmpeg_audio(struct ffmpeg_audio_context *audio)
{
    avcodec_free_context(&audio->codec_ctx);
    av_frame_free(&audio->frame);
    av_audio_fifo_free(audio->fifo);
    audio->fifo = NULL;
    av_packet_free(&audio->packet);
}

// Safely handles NULL pointers, and ensures they're all NULL when done.
static void
destroy_ffmpeg_video(struct ffmpeg_video_context *video)
{
    av_packet_free(&video->packet);
    avcodec_free_context(&video->codec_ctx);
    av_frame_free(&video->frame);
    av_buffer_pool_uninit(&video->frame_buffer_pool);
}

// Does not create the audio AVStream; see capture_video_init_writers().
// Cleans up after itself on failure.
static bool
init_ffmpeg_audio(struct scran_output *st_output, uint32_t audio_encoder_mask)
{
    struct ffmpeg_context *ffmpeg = &st_output->capture.ffmpeg_ctx;

    int ret;
    static const AVChannelLayout channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    static const int             sample_rate    = SCRAN_PIPEWIRE_SAMPLE_RATE;
    static const AVRational      time_base      = { 1, NSEC_PER_SEC };

    assert(channel_layout.nb_channels == SCRAN_PIPEWIRE_N_CHANNELS);

    for (enum audio_encoder_index i = 0; i < ARRAY_LEN(audio_encoders); ++i) {
        if (!(audio_encoder_mask & (1U << i))) {
            continue;
        }

        // AVCodec
        const AVCodec *codec = avcodec_find_encoder_by_name(audio_encoders[i].encoder);
        if (!codec) {
            continue;
        }

        const enum AVSampleFormat *supported_sample_fmts;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61,13,100)
        ret = avcodec_get_supported_config(
            NULL, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
            (const void **)&supported_sample_fmts, NULL
        );
        if (ret < 0) {
            eprintf("WARNING: Failed to get supported sample formats of audio encoder '%s': %s\n", codec->name, av_err2str(ret));
            continue;
        }
#else
        supported_sample_fmts = codec->sample_fmts;
#endif /* LIBAVCODEC_VERSION_INT */
        if (supported_sample_fmts && supported_sample_fmts[0] == AV_SAMPLE_FMT_NONE) {
            eprintf("WARNING: No supported sample formats found for audio encoder '%s'.\n", codec->name);
            continue;
        }

        const enum AVSampleFormat sample_fmt = scran_pipewire_select_sample_format(supported_sample_fmts);
        if (sample_fmt == AV_SAMPLE_FMT_NONE) {
            eprintf("WARNING: No capturable sample format for audio encoder '%s'.\n", codec->name);
            continue;
        }

        // AVCodecContext
        ffmpeg->audio.codec_ctx              = avcodec_alloc_context3(codec);
        if (!ffmpeg->audio.codec_ctx) {
            eprintf("WARNING: Failed to allocate audio encoder context.\n");
            continue;
        }
        if (ffmpeg->format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            ffmpeg->audio.codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        ffmpeg->audio.codec_ctx->sample_rate = sample_rate;
        ffmpeg->audio.codec_ctx->sample_fmt  = sample_fmt;
        ffmpeg->audio.codec_ctx->time_base   = time_base;
        ffmpeg->audio.codec_ctx->ch_layout   = channel_layout;
        ret = avcodec_open2(ffmpeg->audio.codec_ctx, codec, NULL);
        if (ret < 0) {
            eprintf("WARNING: Failed to open audio encoder '%s': %s\n", codec->name, av_err2str(ret));
            goto next_encoder;
        }

        // AVFrame (captured)
        ffmpeg->audio.frame              = av_frame_alloc();
        if (!ffmpeg->audio.frame) {
            eprintf("WARNING: Failed to allocate audio frame.\n");
            goto next_encoder;
        }
        ffmpeg->audio.frame->format      = sample_fmt;
        ffmpeg->audio.frame->sample_rate = sample_rate;
        ffmpeg->audio.frame->ch_layout   = channel_layout;
        ffmpeg->audio.frame->nb_samples  = ffmpeg->audio.codec_ctx->frame_size;
        ret = av_frame_get_buffer(ffmpeg->audio.frame, 0);
        if (ret < 0) {
            eprintf("WARNING: Failed to allocate audio frame buffer for encoder '%s': %s\n", codec->name, av_err2str(ret));
            goto next_encoder;
        }

        eprintf("Using audio codec: %s\n", codec->name);

        break;

next_encoder:
        destroy_ffmpeg_audio(&ffmpeg->audio);
    }
    if (!ffmpeg->audio.codec_ctx) {
        print_unusable_encoders("audio", audio_encoders, ARRAY_LEN(audio_encoders), audio_encoder_mask);
        return false;
    }

    // AVAudioFifo
    ffmpeg->audio.fifo = av_audio_fifo_alloc(
        ffmpeg->audio.codec_ctx->sample_fmt, channel_layout.nb_channels, ffmpeg->audio.codec_ctx->frame_size
    );
    if (!ffmpeg->audio.fifo) {
        eprintf("Error: Failed to allocate audio FIFO.\n");
        goto fail;
    }

    // AVPacket (encoded)
    ffmpeg->audio.packet = av_packet_alloc();
    if (!ffmpeg->audio.packet) {
        eprintf("Error: Failed to allocate audio packet.\n");
        goto fail;
    }

    if (!scran_pipewire_attach(st_output, ffmpeg->audio.codec_ctx->sample_fmt)) {
        goto fail;
    }

    return true;

fail:
    destroy_ffmpeg_audio(&ffmpeg->audio);
    return false;
}

static void
drop_encoders_unsupported_by_format(uint32_t *mask, const struct encoder_info *encoders, size_t count, const AVOutputFormat *oformat)
{
    for (size_t i = 0; i < count; ++i) {
        uint32_t mask_bit = 1U << i;
        if (*mask & mask_bit) {
            const AVCodecDescriptor *codec = avcodec_descriptor_get_by_name(encoders[i].codec);
            if (!codec || !avformat_query_codec(oformat, codec->id, FF_COMPLIANCE_NORMAL)) {
                *mask &= ~mask_bit;
            }
        }
    }
}

static bool
get_video_config(struct video_config *config)
{
    *config = m_video_config;

    config->oformat = av_guess_format(video_formats[config->format].muxer_name, NULL, NULL);
    if (!config->oformat) {
        eprintf("Error: av_guess_format('%s') failed.\n", video_formats[config->format].muxer_name);
        scran_notify_error("provided video format is not available");
        return false;
    }

    // TODO: figure out a consistent responsibility split between what parts of the init sends dbus
    // notifications vs just stderr prints etc.

    drop_encoders_unsupported_by_format(&config->video_encoders, video_encoders, ARRAY_LEN(video_encoders), config->oformat);
    if (!config->video_encoders) {
        eprintf("Error: No provided video encoders are supported by the provided format (%s).\n", video_formats[config->format].muxer_name);
        scran_notify_error("video encoder not supported by format");
        return false;
    }
    drop_encoders_unsupported_by_format(&config->audio_encoders, audio_encoders, ARRAY_LEN(audio_encoders), config->oformat);

    return true;
}

#define GOP_SIZE 120

// TODO:
//  - Encoding parameters:
//      - Let user override the encoding parameters
//      - Decide on good defaults
//      - Store defaults in a const struct (or whatever format libav prefers)
//
//  - Don't pass scran_output. Just width/height etc.
//
bool
capture_video_init_writers(struct scran_output *st_output, const BLPointI dimensions)
{
    struct scran_output_capture *capture = &st_output->capture;
    struct ffmpeg_context       *ffmpeg  = &capture->ffmpeg_ctx;

    // TODO: Cache the result?
    struct video_config config;
    if (!get_video_config(&config)) {
        return false;
    }

    assert(!capture->audio_active);
    const char *output_filepath = NULL;
    int ret;

    ffmpeg->failure_mask = SCRAN_VIDEO_FAILURE_NONE;

    // AVFrame (converted, ready to be fed to encoder)
    //
    // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be
    // divisible by 2. TODO: Also update selection area visuals to this width.
    int frame_width  = dimensions.x & ~0b1;
    int frame_height = dimensions.y & ~0b1;
    ffmpeg->video.frame                  = av_frame_alloc();
    if (!ffmpeg->video.frame) {
        eprintf("Error: Failed to allocate video frame.\n");
        goto fail;
    }
    ffmpeg->video.frame->width           = frame_width;
    ffmpeg->video.frame->height          = frame_height;
    // NOTE: Some pixel formats (and some file formats), e.g. YUV420P, require
    // even-numbered (or some other multiplier) height and/or width.
    // TODO: Support other pixel formats. We currently only need (planar) yuv420
    // due to using scranrot.
    ffmpeg->video.frame->format          = AV_PIX_FMT_YUV420P;
    // scranrot outputs full-range BT.709 Y'CbCr. The capture source is desktop
    // RGB, so we use BT.709 video metadata for broad playback compatibility.
    ffmpeg->video.frame->color_range     = AVCOL_RANGE_JPEG;
    ffmpeg->video.frame->colorspace      = AVCOL_SPC_BT709;
    ffmpeg->video.frame->color_primaries = AVCOL_PRI_BT709;
    ffmpeg->video.frame->color_trc       = AVCOL_TRC_BT709;


    // AVBufferPool
    ffmpeg->video.frame_buffer_pool = av_buffer_pool_init(
        // Keep the 16 bytes padding for sanity's sake, even if currently unnecessary.
        //   See AVFrame.data comment.
        scranrot_yuv420_buffer_size(frame_width, frame_height) + 16,
        NULL
    );
    if (!ffmpeg->video.frame_buffer_pool) {
        eprintf("Error: Failed to allocate video frame buffer pool.\n");
        goto fail;
    }

    // AVFormat
    ret = avformat_alloc_output_context2(&ffmpeg->format_ctx, config.oformat, NULL, NULL);
    if (ret < 0) {
        eprintf("Error: Failed to allocate output format context: %s\n", av_err2str(ret));
        goto fail;
    }
    switch (config.format) {
    case VIDEO_FORMAT_MP4:
#if !defined LIBAVFORMAT_VERSION_INT || (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(61,4,100))
        // Best we can do for seeking/playback-duration compatibility without remuxing or custom
        // "hybrid mp4" implementation, if hybrid_fragmented is unavailable.
        ret = av_opt_set(ffmpeg->format_ctx->priv_data, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
#else
        ret = av_opt_set(ffmpeg->format_ctx->priv_data, "movflags", "hybrid_fragmented", 0);
#endif /* LIBAVFORMAT_VERSION_INT */
        if (ret < 0) {
            eprintf("Error: Failed to set video muxer movflags: %s\n", av_err2str(ret));
            goto fail;
        }
        // Earlier versions were not implemented correctly for hybrid_fragmented
        // (ffmpeg commit 920071355dd82a74ab9bfd879d42401702b83a5e)
#if LIBAVFORMAT_VERSION_MAJOR >= 62
        // Our mp4 muxer may shift the PTS/DTS timelines forwards to ensure that the
        // first DTS is non-negative. Some players/programs need an explicit
        // "editlist" in the moov, which specifies the timestamp at which the actual
        // playback is supposed to start. For example Adobe Premiere refuses to open
        // the file otherwise.
        ret = av_opt_set(ffmpeg->format_ctx->priv_data, "use_editlist", "1", 0);
        if (ret < 0) {
            eprintf("Error: Failed to set video muxer use_editlist: %s\n", av_err2str(ret));
            goto fail;
        }
#endif
        break;
    case VIDEO_FORMAT_MKV:
        ;
        // MKV is stored fragmented by default (Clusters).
        _Static_assert(
            GOP_SIZE >= 120,
           "gop_size effectively caps cluster_time_limit. set it explicitly, or update the assert if"
           " the new gop_size is still an acceptable limit."
        );
        break;
    }


    // AVCodecContext (encoder)
    //
    // In preference order.
    for (enum video_encoder_index i = 0; i < ARRAY_LEN(video_encoders); ++i) {
        if (!(config.video_encoders & (1U << i))) {
            continue;
        }
        const struct AVCodec *codec = avcodec_find_encoder_by_name(video_encoders[i].encoder);
        if (!codec) {
            continue;
        }

        ffmpeg->video.codec_ctx = avcodec_alloc_context3(codec);
        if (!ffmpeg->video.codec_ctx) {
            eprintf("WARNING: Failed to allocate video encoder context.\n");
            continue;
        }
        if (ffmpeg->format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            ffmpeg->video.codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        // -- Values tied to encoder input/environment --
        ffmpeg->video.codec_ctx->width           = ffmpeg->video.frame->width;
        ffmpeg->video.codec_ctx->height          = ffmpeg->video.frame->height;
        ffmpeg->video.codec_ctx->pix_fmt         = ffmpeg->video.frame->format;
        ffmpeg->video.codec_ctx->color_range     = ffmpeg->video.frame->color_range;
        ffmpeg->video.codec_ctx->colorspace      = ffmpeg->video.frame->colorspace;
        ffmpeg->video.codec_ctx->color_primaries = ffmpeg->video.frame->color_primaries;
        ffmpeg->video.codec_ctx->color_trc       = ffmpeg->video.frame->color_trc;
        // Nominal maximum framerate. Effect is encoder-specific.
        ffmpeg->video.codec_ctx->framerate       = (AVRational){
            // Wayland's wl_output::mode.refresh allows 0.
            st_output->mode.refresh_rate_mHz > 0
                ? st_output->mode.refresh_rate_mHz
                : 60 * MILLIHZ_PER_HZ,
            MILLIHZ_PER_HZ
        };
        // Wayland's frame::presentation_time() has nanosecond precision.
        ffmpeg->video.codec_ctx->time_base       = (AVRational){ 1, NSEC_PER_SEC };
        // -- Values to be tweaked (may depend on encoder/format) --
        // NOTE: B-frames not be supported by all codecs (e.g. libopenh264?)
        ffmpeg->video.codec_ctx->max_b_frames    = 2;
        ffmpeg->video.codec_ctx->gop_size        = GOP_SIZE;
        switch (i) {
        case VIDEO_ENCODER_LIBX264:
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "crf", "20", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libx264 crf: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "preset", "superfast", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libx264 preset: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            break;
        case VIDEO_ENCODER_LIBOPENH264:
            break;
        case VIDEO_ENCODER_LIBVPX_VP9:
            ffmpeg->video.codec_ctx->bit_rate = 0;
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "crf", "30", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libvpx-vp9 crf: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "deadline", "realtime", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libvpx-vp9 deadline: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "row-mt", "1", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libvpx-vp9 row-mt: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "cpu-used", "8", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libvpx-vp9 cpu-used: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            break;
        case VIDEO_ENCODER_LIBSVTAV1:
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "crf", "30", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libsvtav1 crf: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            ret = av_opt_set(ffmpeg->video.codec_ctx->priv_data, "preset", "11", 0);
            if (ret < 0) {
                eprintf("WARNING: Failed to set libsvtav1 preset: %s\n", av_err2str(ret));
                goto next_encoder;
            }
            break;
        }
        ret = avcodec_open2(ffmpeg->video.codec_ctx, codec, NULL);
        if (ret < 0) {
            eprintf("WARNING: Failed to open video encoder '%s': %s\n", codec->name, av_err2str(ret));
            goto next_encoder;
        }

        eprintf("Using video codec: %s\n", codec->name);
        break;

next_encoder:
        avcodec_free_context(&ffmpeg->video.codec_ctx);
    }
    if (!ffmpeg->video.codec_ctx) {
        print_unusable_encoders("video", video_encoders, ARRAY_LEN(video_encoders), config.video_encoders);
        goto fail;
    }

    assert(ffmpeg->video.frame->width  != 0);
    assert(ffmpeg->video.frame->height != 0);


    // AVPacket (encoded)
    ffmpeg->video.packet = av_packet_alloc();
    if (!ffmpeg->video.packet) {
        eprintf("Error: Failed to allocate video packet.\n");
        goto fail;
    }


    // AVStream (video)
    AVStream *_av_stream = avformat_new_stream(ffmpeg->format_ctx, NULL);
    if (!_av_stream) {
        eprintf("Error: Failed to create video stream.\n");
        goto fail;
    }
    assert(_av_stream == ffmpeg->format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_VIDEO]);
    // NOTE: Requested time_base. Final time_base will have been selected by
    // libav after write_header. av_packet_rescale_ts() exists to convert from
    // Encoder timestamps to Format timestamps prior to handing off the packet.
    assert(ffmpeg->video.codec_ctx->framerate.num != 0 && ffmpeg->video.codec_ctx->framerate.den != 0);
    _av_stream->time_base = av_inv_q(ffmpeg->video.codec_ctx->framerate);
    ret = avcodec_parameters_from_context(_av_stream->codecpar, ffmpeg->video.codec_ctx);
    if (ret < 0) {
        eprintf("Error: Failed to set video stream parameters: %s\n", av_err2str(ret));
        goto fail;
    }


    if (!g_state.options.disable_audio_capture && !capture->audio_disable_modifier_active) {
        if (init_ffmpeg_audio(st_output, config.audio_encoders)) {
            capture->audio_active = true;
        } else {
            eprintf("WARNING: Failed to init audio capture.\n");
        }
    }


    // AVStream (audio)
    //   Not created in init_ffmpeg_audio(), since freeing a stream requires
    //   freeing the entire format context.
    //   TODO: Recreate the format context if this block fails?
    if (capture->audio_active) {
        AVStream *audio_stream = avformat_new_stream(ffmpeg->format_ctx, NULL);
        if (!audio_stream) {
            eprintf("Error: Failed to create audio stream.\n");
            goto fail;
        }
        assert(audio_stream == ffmpeg->format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_AUDIO]);
        ret = avcodec_parameters_from_context(audio_stream->codecpar, ffmpeg->audio.codec_ctx);
        if (ret < 0) {
            eprintf("Error: Failed to set audio stream parameters: %s\n", av_err2str(ret));
            goto fail;
        }
    }


    // Finalize AVFormat and initialize the output file

    if (g_state.options.output_to_stdout) {
        assert(scran_stdout_check_reservation(&st_output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO));
        output_filepath = "pipe:1";
    } else {
        char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX];
        {
            const char *extensions = ffmpeg->format_ctx->oformat->extensions;
            if (!extensions || extensions[0] == '\0') {
                eprintf("Error: File extension not found.\n");
                goto fail;
            }

            size_t extension_len = strcspn(extensions, ",");
            if (extension_len >= sizeof(file_extension)) {
                eprintf("Error: File extension too long.\n");
                goto fail;
            }
            memcpy(file_extension, extensions, extension_len);
            file_extension[extension_len] = '\0';
        }

        output_filepath = scran_prepare_video_output_path(&g_state.options, file_extension);
        if (!output_filepath) {
            goto fail;
        }
    }

    ffmpeg->format_ctx->url = av_strdup(output_filepath);
    if (!ffmpeg->format_ctx->url) {
        eprintf("Error: Failed to allocate URL.\n");
        goto fail;
    }

    assert(!((ffmpeg->format_ctx)->oformat->flags & AVFMT_NOFILE));
    ret = avio_open(&(ffmpeg->format_ctx)->pb, output_filepath, AVIO_FLAG_WRITE);
    if (ret < 0) {
        eprintf("Failed to create video file '%s': %s\n", output_filepath, av_err2str(ret));
        goto fail;
    }
    ret = avformat_write_header(ffmpeg->format_ctx, NULL);
    if (ret < 0) {
        eprintf("Failed to write file header (filepath: %s): %s\n", output_filepath, av_err2str(ret));
        goto fail;
    }

    return true;

fail:
    // TODO: unlink after close?
    if (ffmpeg->format_ctx && ffmpeg->format_ctx->pb && !g_state.options.output_to_stdout) {
        unlink(output_filepath);
    }
    if (capture->audio_active) {
        scran_pipewire_detach(st_output);
        capture->audio_active = false;
    }
    capture_video_destroy_writers(st_output);
    return false;
}

static bool
write_packet(struct ffmpeg_context *ffmpeg, AVPacket *pkt, const char *stream_name)
{
    const int ret = av_interleaved_write_frame(ffmpeg->format_ctx, pkt);
    if (ret < 0) {
        eprintf("Error: Failed to write %s packet: %s\n", stream_name, av_err2str(ret));
        ffmpeg->failure_mask |= SCRAN_VIDEO_FAILURE_MUX;
        return false;
    }
    return true;
}

bool
capture_video_drain_write_queue(struct ffmpeg_context *ffmpeg)
{
    return write_packet(ffmpeg, NULL, "flush");
}

// Can be used for both audio and video encoders
bool
capture_video_drain_codec(
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
        eprintf("Error: Failed to start draining %s encoder: %s\n", stream_name, av_err2str(ret));
        return false;
    }

    for (;;) {
        ret = avcodec_receive_packet(codec_ctx, packet);
        if (ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            eprintf("Error: Failed while draining %s encoder: %s\n", stream_name, av_err2str(ret));
            return false;
        }

        if (!write_packet_fn(st_output, packet)) {
            return false;
        }
    }
}


// Safely handles partially initialized writers, and ensures all pointers are
// NULL when done.
void
capture_video_destroy_writers(struct scran_output *st_output)
{
    struct ffmpeg_context *ffmpeg = &st_output->capture.ffmpeg_ctx;

    destroy_ffmpeg_audio(&ffmpeg->audio);
    destroy_ffmpeg_video(&ffmpeg->video);

    if (ffmpeg->format_ctx) {
        int ret = avio_closep(&ffmpeg->format_ctx->pb);
        if (ret < 0) {
            eprintf("Error: Failed to close video output: %s\n", av_err2str(ret));
        }
    }
    // Freeing the format context frees the linked streams for us.
    avformat_free_context(ffmpeg->format_ctx);
    ffmpeg->format_ctx = NULL;
}

bool
capture_video_write_video_packet(
    struct scran_output *output,
    AVPacket *pkt // Encoded frame
) {
    struct ffmpeg_context *ffmpeg = &output->capture.ffmpeg_ctx;

    assert(pkt != NULL);

    const AVStream *const _av_stream = ffmpeg->format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_VIDEO];

    pkt->stream_index = SCRAN_AV_FORMAT_STREAM_IDX_VIDEO;
    assert(pkt->stream_index == _av_stream->index);

    // NOTE: This is doing the work of av_packet_rescale_ts(), but with
    // asserts instead of conditionals. Just switch to that or to
    // if-statements if indeterminism becomes necessary.
    assert(pkt->pts != AV_NOPTS_VALUE);
    pkt->pts = av_rescale_q(pkt->pts, ffmpeg->video.codec_ctx->time_base, _av_stream->time_base);
    assert(pkt->dts != AV_NOPTS_VALUE);
    pkt->dts = av_rescale_q(pkt->dts, ffmpeg->video.codec_ctx->time_base, _av_stream->time_base);

    // We don't use durations for VFR.
    //     XXX: This gives a warning on the last frame before
    //     avformat_write_header(). Not sure if worth fixing.
    assert(pkt->duration <= 0);

    return write_packet(ffmpeg, pkt, "video");
}

bool
capture_video_write_audio_packet(
    struct scran_output *st_output,
    AVPacket *pkt // Encoded frame
) {
    struct ffmpeg_context *ffmpeg = &st_output->capture.ffmpeg_ctx;

    assert(pkt != NULL);

    const AVStream *const av_stream = ffmpeg->format_ctx->streams[SCRAN_AV_FORMAT_STREAM_IDX_AUDIO];
    pkt->stream_index = SCRAN_AV_FORMAT_STREAM_IDX_AUDIO;
    assert(pkt->stream_index == av_stream->index);

    // NOTE: This is doing the work of av_packet_rescale_ts(), but with
    // asserts instead of conditionals. Just switch to that or to
    // if-statements if indeterminism becomes necessary.
    assert(pkt->pts != AV_NOPTS_VALUE);
    pkt->pts = av_rescale_q(pkt->pts, ffmpeg->audio.codec_ctx->time_base, av_stream->time_base);
    assert(pkt->dts != AV_NOPTS_VALUE);
    pkt->dts = av_rescale_q(pkt->dts, ffmpeg->audio.codec_ctx->time_base, av_stream->time_base);
    assert(pkt->duration != AV_NOPTS_VALUE);
    pkt->duration = av_rescale_q(pkt->duration, ffmpeg->audio.codec_ctx->time_base, av_stream->time_base);

    return write_packet(ffmpeg, pkt, "audio");
}

bool
capture_video_write_video_frame(
    struct capture_view view,
    const struct capture_buffer_area_context *buffer_area_ctx
) {
    struct scran_output   *output = view.output;
    struct ffmpeg_context *ffmpeg = &output->capture.ffmpeg_ctx;

    AVFrame *frame = ffmpeg->video.frame;

    // Crop and convert
    {
        // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be divisible by 2.
        // XXX TODO: Collect this bit zeroing logic somehow? (Duplicated in capture_video_init_writers.)
        const int area_w_px = blboxi_width_abs_unsafe(buffer_area_ctx->area_px) & ~0b1;
        const int area_h_px = blboxi_height_abs_unsafe(buffer_area_ctx->area_px) & ~0b1;

        uint32_t rgba32_shuffle = wl_shm_format_to_scranrot_yuv_rgba32_shuffle(view.session_ctx->shm_format);
        if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
            eprintf(
                "WARNING: Output's pixel format (%x) not recognized. Please report this as a bug. Attempting anyways...\n",
                view.session_ctx->shm_format
            );
            rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
        }

        // XXX: Scranrot does not support flipped transforms yet, so we just
        // record it flipped for now, rather than blocking capture entirely.
        enum wl_output_transform transform = wl_output_transform_without_flip(view.frame_ctx->source_transform);

        assert(frame->width == get_transformed_width(area_w_px, area_h_px, transform));
        assert(frame->height == get_transformed_height(area_w_px, area_h_px, transform));

        assert(!frame->buf[0]);
        frame->buf[0] = av_buffer_pool_get(ffmpeg->video.frame_buffer_pool);
        if (!frame->buf[0]) {
            eprintf("Error: Failed to allocate video frame buffer.\n");
            goto err_video_stream;
        }

        if (!scranrot_transform_framebuffer_to_yuv420(
                buffer_area_ctx->area_start_address,
                area_w_px,
                area_h_px,
                buffer_area_ctx->source_row_bytes,
                frame->buf[0]->data,
                rgba32_shuffle,
                wl_output_transform_to_scranrot(transform),
                &frame->data[0], &frame->linesize[0],
                &frame->data[1], &frame->linesize[1],
                &frame->data[2], &frame->linesize[2]
            )
        ) {
            eprintf("Error: scranrot failed to convert framebuffer to yuv\n");
            // TODO: merge the unrefs into one path
            av_buffer_unref(&frame->buf[0]);
            goto err_video_stream;
        }
        frame->pts = view.frame_ctx->presentation_time_nsec - output->capture.video_presentation_time_nsec_start;
    }

    // Encode
    int err = avcodec_send_frame(ffmpeg->video.codec_ctx, frame);
    av_buffer_unref(&frame->buf[0]);
    if (err < 0) {
        eprintf("Error: avcodec_send_frame: %s\n", av_err2str(err));
        goto err_video_stream;
    }
    while (err >= 0) {
        err = avcodec_receive_packet(ffmpeg->video.codec_ctx, ffmpeg->video.packet);
        if (err == AVERROR_EOF || err == AVERROR(EAGAIN)) {
            break;
        } else if (err < 0) {
            eprintf("Error: avcodec_receive_packet: %s\n", av_err2str(err));
            goto err_video_stream;
        }

        if (!capture_video_write_video_packet(output, ffmpeg->video.packet)) {
            goto err;
        }

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    av_packet_unref(ffmpeg->video.packet);
    return true;

err_video_stream:
    ffmpeg->failure_mask |= SCRAN_VIDEO_FAILURE_VIDEO_STREAM;
err:
    // write_packet() records MUX failure on its own.
    return false;
}
