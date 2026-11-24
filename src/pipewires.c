#include <stdbool.h>
#include <sys/epoll.h>
#include <assert.h>
#include <stdatomic.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/buffer/meta.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/mathematics.h>

#include "spa/param/audio/raw.h"
#include "state-util.h"
#include "state.h"
#include "pipewires.h"
#include "capture.h"
#include "print.h"
#include "util/lib-interop.h"


static const enum AVSampleFormat m_supported_formats[] = {
    [0] = AV_SAMPLE_FMT_FLTP, // Default if the encoder accepts any format
    AV_SAMPLE_FMT_FLT,

    AV_SAMPLE_FMT_NONE,
};


static struct {
    struct pw_loop       *loop;
    struct pw_stream     *stream;
    struct pw_context    *ctx;
    struct pw_core       *core;
    struct spa_hook       stream_listener;
    enum spa_audio_format format;
    int loop_fd;
    int epoll_fd;
    bool pw_inited;

    bool started;

    _Static_assert(MAX_OUTPUTS <= 64, "max outputs must fit in mask");
    uint64_t active_outputs_mask; // as in `struct scran_outputs`
} m_state = {
    .epoll_fd = -1,
    .loop_fd = -1,
    .format = SPA_AUDIO_FORMAT_UNKNOWN,
};


static inline uint64_t
get_output_mask(struct scran_output *output) {
    return 1ULL << get_output_array_index(output);
}


static void
on_process(void *data)
{
    struct pw_buffer *pw_buf = pw_stream_dequeue_buffer(m_state.stream);
    if (!pw_buf) {
        eprintf("Pipewire out of buffers\n");
        return;
    }
    struct spa_buffer      *spa_buf     = pw_buf->buffer;
    struct spa_meta_header *meta_header = spa_buffer_find_meta_data(spa_buf, SPA_META_Header, sizeof(*meta_header));

    // XXX: Cast overflows at ~292 years uptime.
    const int64_t  pts_incoming                  = (meta_header != NULL) ? meta_header->pts : (int64_t)pw_buf->time;
    const uint32_t spa_buf_chunk_size            = spa_buf->datas[0].chunk->size;
    uint32_t       spa_buf_n_samples_per_channel = 0;

    switch (m_state.format) {
    case SPA_AUDIO_FORMAT_F32:
        assert(spa_buf->n_datas == 1);
        spa_buf_n_samples_per_channel = spa_buf_chunk_size / (sizeof(float) * SCRAN_PIPEWIRE_N_CHANNELS);
        break;
    case SPA_AUDIO_FORMAT_F32P:
        assert(spa_buf->n_datas == SCRAN_PIPEWIRE_N_CHANNELS);
        spa_buf_n_samples_per_channel = spa_buf_chunk_size / sizeof(float);
        break;
    default:
        assert(false && "unsupported format");
    }

    struct _planes {
        // XXX TODO: FR/FL should be mapped to 0/1 dynamically.
        const float *samples[SCRAN_PIPEWIRE_N_CHANNELS];
    };
    struct _planes spa_buf_planes;

    for (uint32_t i = 0; i < spa_buf->n_datas; ++i) {
        float *samples = spa_buf->datas[i].data;
        if (!samples) {
            goto done;
        }
        spa_buf_planes.samples[i] = samples;
    }

    FOR_EACH_OUTPUT(i, output) {
        if (!(get_output_mask(output) & m_state.active_outputs_mask)) {
            continue;
        }
        if (!capture_video_is_live(output)) {
            continue;
        }

        struct scran_output_capture *capture = &output->capture;
        struct ffmpeg_context       *ffmpeg  = &capture->ffmpeg_ctx;

        if (ffmpeg->failure_mask & (SCRAN_VIDEO_FAILURE_AUDIO_STREAM | SCRAN_VIDEO_FAILURE_MUX)) {
            continue;
        }

        int64_t        pts_video_start       = capture->video_presentation_time_nsec_start;
        int64_t        pts                   = pts_incoming;
        int64_t        n_samples_per_channel = spa_buf_n_samples_per_channel;
        // NOTE: Keep this a (shallow) copy, since we will be mutating the sample pointers!
        struct _planes planes_copy           = spa_buf_planes;

        // Discard samples that were presented before video capture started.
        // This should always be a no-op after the initial sample chunk(s).
        // Primarily needed for new outputs attaching to an already-started
        // pipewire loop, but also makes initialization ordering/timing less
        // delicate for cold-starts.
        {
            const int64_t pts_delta = pts - pts_video_start;
            if (pts_delta < 0) {
                int64_t nsec_to_skip = -pts_delta;
                const int64_t n_samples_to_skip_per_channel = av_rescale_rnd(nsec_to_skip, SCRAN_PIPEWIRE_SAMPLE_RATE, NSEC_PER_SEC, AV_ROUND_UP);
                nsec_to_skip = av_rescale(n_samples_to_skip_per_channel, NSEC_PER_SEC, SCRAN_PIPEWIRE_SAMPLE_RATE);

                pts += nsec_to_skip;
                n_samples_per_channel -= n_samples_to_skip_per_channel;

                switch (m_state.format) {
                case SPA_AUDIO_FORMAT_F32:
                    planes_copy.samples[0] += n_samples_to_skip_per_channel * SCRAN_PIPEWIRE_N_CHANNELS;
                    break;
                case SPA_AUDIO_FORMAT_F32P:
                    for (uint32_t i = 0; i < SCRAN_PIPEWIRE_N_CHANNELS; ++i) {
                        planes_copy.samples[i] += n_samples_to_skip_per_channel;
                    }
                    break;
                default:
                    assert(false && "unsupported format");
                }

                if (n_samples_per_channel < 0) { // Entire chunk is before video start
                    continue;
                }
            }
        }

        // NOTE: n_samples_leftover must be adjusted if moved to after av_audio_fifo_write()
        int n_samples_leftover = av_audio_fifo_size(ffmpeg->audio.fifo);
        int ret = av_audio_fifo_write(ffmpeg->audio.fifo, (void **)planes_copy.samples, n_samples_per_channel);
        if (ret < 0) {
            eprintf("Error: av_audio_fifo_write: %s\n", av_err2str(ret));
            goto output_err_audio_stream;
        }

        int64_t pts_fifo_start =
            pts - pts_video_start - av_rescale(n_samples_leftover, NSEC_PER_SEC, SCRAN_PIPEWIRE_SAMPLE_RATE);
        int frame_size = ffmpeg->audio.codec_ctx->frame_size;

        assert(frame_size == ffmpeg->audio.frame->nb_samples);

        int64_t pts_curr = pts_fifo_start;
        while (av_audio_fifo_size(ffmpeg->audio.fifo) >= frame_size) {
            ret = av_audio_fifo_read(
                ffmpeg->audio.fifo,
                (void **)ffmpeg->audio.frame->data,
                frame_size
            );
            if (ret < 0) {
                eprintf("Error: av_audio_fifo_read: %s\n", av_err2str(ret));
                goto output_err_audio_stream;
            }
            assert(ret == frame_size);

            ffmpeg->audio.frame->pts = pts_curr;

            int ret_enc = avcodec_send_frame(ffmpeg->audio.codec_ctx, ffmpeg->audio.frame);
            if (ret_enc < 0) {
                eprintf("Error: avcodec_send_frame: %s\n", av_err2str(ret_enc));
                goto output_err_audio_stream;
            }

            while (ret_enc >= 0) {
                ret_enc = avcodec_receive_packet(ffmpeg->audio.codec_ctx, ffmpeg->audio.packet);
                if (ret_enc == AVERROR_EOF || ret_enc == AVERROR(EAGAIN)) {
                    break;
                } else if (ret_enc < 0) {
                    eprintf("Error: avcodec_receive_packet: %s\n", av_err2str(ret_enc));
                    goto output_err_audio_stream;
                }
                if (!capture_video_write_audio_packet(output, ffmpeg->audio.packet)) {
                    goto output_err;
                }
            }

            pts_curr += av_rescale(frame_size, NSEC_PER_SEC, SCRAN_PIPEWIRE_SAMPLE_RATE);
        }

        goto output_done;

output_err_audio_stream:
        // TODO: Only stop recording audio.
        ffmpeg->failure_mask |= SCRAN_VIDEO_FAILURE_AUDIO_STREAM;
output_err:
        // write_packet() records MUX failure on its own.
        capture_video_request_stop(output);
output_done:
        // functions called above make their own reference if necessary
        av_packet_unref(ffmpeg->audio.packet);
    }

done:
    pw_stream_queue_buffer(m_state.stream, pw_buf);
}


static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
    // TODO: handle param_changed ?
};

// Deinit the pipewire listeners etc. without full pipewire deinit.
//
// scran_pipewire_start() reinits and reconnects.
// scran_pipewire_destroy() does a full deinit.
static void
scran_pipewire_reset()
{
    m_state.started = false;

    if (m_state.stream) {
        spa_hook_remove(&m_state.stream_listener);
        pw_stream_disconnect(m_state.stream);
        pw_stream_destroy(m_state.stream);
        m_state.stream = NULL;
    }

    if (m_state.core) {
        pw_core_disconnect(m_state.core);
        m_state.core = NULL;
    }

    if (m_state.ctx) {
        pw_context_destroy(m_state.ctx);
        m_state.ctx = NULL;
    }

    if (m_state.loop_fd != -1) {
        epoll_ctl(m_state.epoll_fd, EPOLL_CTL_DEL, m_state.loop_fd, NULL);
        m_state.loop_fd  = -1;
    }

    if (m_state.loop) {
        pw_loop_leave(m_state.loop);
        pw_loop_destroy(m_state.loop);
        m_state.loop  = NULL;
    }
}

// **Never** call this from within a pipewire callback unless you can guarantee
// that the pipewire state will not get reset.
void
scran_pipewire_detach(struct scran_output *output)
{
    uint64_t output_mask = get_output_mask(output);

    assert(m_state.active_outputs_mask & output_mask);
    m_state.active_outputs_mask &= ~output_mask;

    if (!m_state.active_outputs_mask) {
        scran_pipewire_reset();
    }
}

// Full deinit
void
scran_pipewire_destroy()
{
    if (!m_state.pw_inited) {
        return;
    }
    scran_pipewire_reset();
    pw_deinit();
    m_state.pw_inited = false;
}

void
scran_pipewire_prepare(int epoll_fd)
{
    m_state.epoll_fd = epoll_fd;
}

// scran_pipewire_prepare() must be called first to set epoll fd.
// scran_pipewire_detach()/scran_pipewire_reset() cleans up.
//
// On failure, returns false and cleans up after itself.
static bool
scran_pipewire_start(enum spa_audio_format format)
{
    if (m_state.started) {
        if (m_state.format != format) {
            eprintf("Error: scran_pipewire_start() got different format than the previous call\n");
        }
        return true;
    }

    // NOTE: We init it lazily here, since not all scran sessions will care
    // about audio. pw_deinit() is called in this file/module's destroy function.
    if (!m_state.pw_inited) {
        pw_init(NULL, NULL);
        m_state.pw_inited = true;
    }

    m_state.loop = pw_loop_new(NULL);
    if (!m_state.loop) {
        eprintf("WARNING: Failed to create PipeWire loop\n");
        goto fail;
    }
    pw_loop_enter(m_state.loop);
    m_state.loop_fd = pw_loop_get_fd(m_state.loop);

    assert(m_state.epoll_fd != -1);
    struct epoll_event epoll_event = {
        .events = EPOLLIN,
        .data.fd = m_state.loop_fd
    };
    if (epoll_ctl(m_state.epoll_fd, EPOLL_CTL_ADD, m_state.loop_fd, &epoll_event) < 0) {
        eprintf("WARNING: Failed to add PipeWire loop fd to epoll: %s\n", strerror(errno));
        goto fail;
    }

    m_state.ctx = pw_context_new(m_state.loop, NULL, 0);
    if (!m_state.ctx) {
        eprintf("WARNING: Failed to create PipeWire context: %s\n", strerror(errno));
        goto fail;
    }

    m_state.core = pw_context_connect(m_state.ctx , NULL, 0);
    if (!m_state.core) {
        eprintf("WARNING: Failed to connect to PipeWire daemon: %s\n", strerror(errno));
        goto fail;
    }

    // NOTE: pw_stream takes ownership of this. Don't free.
    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE         , "Audio"  ,
        PW_KEY_MEDIA_CATEGORY     , "Capture",
        PW_KEY_MEDIA_ROLE         , "Screen" ,
        PW_KEY_STREAM_CAPTURE_SINK, "true"   ,
        NULL
    );
    if (!props) {
        eprintf("WARNING: Failed to allocate PipeWire stream properties\n");
        goto fail;
    }
    m_state.stream = pw_stream_new(m_state.core, "scran-audio-capture", props);
    if (!m_state.stream) {
        eprintf("WARNING: Failed to create PipeWire stream\n");
        goto fail;
    }

    m_state.format = format;

    pw_stream_add_listener(m_state.stream, &m_state.stream_listener, &stream_events, NULL);

    // Connect the stream
    {
        uint8_t buffer[1024];
        struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const struct spa_pod *params[] =  {
            spa_format_audio_raw_build(
                &builder,
                SPA_PARAM_EnumFormat,
                &(struct spa_audio_info_raw){
                    .format   = format,
                    .rate     = SCRAN_PIPEWIRE_SAMPLE_RATE,
                    .channels = SCRAN_PIPEWIRE_N_CHANNELS
                }
            )
        };
        int err = pw_stream_connect(
            m_state.stream,
            SPA_DIRECTION_INPUT,
            PW_ID_ANY,
            PW_STREAM_FLAG_AUTOCONNECT
            | PW_STREAM_FLAG_MAP_BUFFERS,
            params,
            sizeof(params) / sizeof(params[0])
        );
        if (err < 0) {
            eprintf("Error: Failed to connect pipewire stream (%d: %s).\n", err, strerror(-err));
            goto fail;
        }
        DEBUG("Pipewire stream connected.\n");
    }

    m_state.started = true;
    return true;

fail:
    scran_pipewire_reset();
    return false;
}

// encoder_formats == NULL means any format is accepted.
// encoder_formats is terminated by AV_SAMPLE_FMT_NONE
//     See avcodec_get_supported_config()
//
// Returns AV_SAMPLE_FMT_NONE if none of encoder_formats can be captured.
enum AVSampleFormat
scran_pipewire_select_sample_format(const enum AVSampleFormat *encoder_formats)
{
    if (!encoder_formats) {
        return m_supported_formats[0];
    }

    for (const enum AVSampleFormat *enc_fmt = encoder_formats; *enc_fmt != AV_SAMPLE_FMT_NONE; ++enc_fmt) {
        for (size_t i = 0; i < ARRAY_LEN(m_supported_formats); ++i) {
            if (*enc_fmt == m_supported_formats[i]) {
                return *enc_fmt;
            }
        }
    }

    return AV_SAMPLE_FMT_NONE;
}

// sample_format must come from scran_pipewire_select_sample_format().
bool
scran_pipewire_attach(struct scran_output *output, enum AVSampleFormat sample_format)
{
    if (!scran_pipewire_start(ffmpeg_sample_format_to_pipewire(sample_format))) {
        return false;
    }

    uint64_t output_mask = get_output_mask(output);

    assert(!(m_state.active_outputs_mask & output_mask));
    m_state.active_outputs_mask |= output_mask;

    return true;
}

bool
scran_pipewire_update(int fd_ready)
{
    if (fd_ready != m_state.loop_fd) {
        return true;
    }

    assert(m_state.loop);

    int err = pw_loop_iterate(m_state.loop, 0);
    if (err < 0) {
        eprintf("Error: Failed to iterate pipewire loop: %s\n", strerror(-err));
        FOR_EACH_OUTPUT(i, output) {
            if ((get_output_mask(output) & m_state.active_outputs_mask)
                && capture_video_is_live(output)
            ) {
                output->capture.ffmpeg_ctx.failure_mask |= SCRAN_VIDEO_FAILURE_AUDIO_STREAM;
                capture_video_request_stop(output);
            }
        }
        return false;
    }

    return true;
}
