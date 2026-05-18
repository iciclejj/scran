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

#include "pipewire/context.h"
#include "pipewire/loop.h"
#include "state.h"
#include "pipewires.h"
#include "capture.h"
#include "print.h"


extern struct scran g_state;


static struct {
    struct pw_loop               *loop;
    struct pw_stream             *stream;
    struct pw_context            *ctx;
    struct pw_core               *core;
    struct spa_hook               stream_listener;
    struct capture_frame_context *userdata;
    enum spa_audio_format format;
    int loop_fd;
    int epoll_fd;
    bool pw_inited;
} m_state = {
    .epoll_fd = -1,
    .loop_fd = -1,
};


static void
on_process(void *data)
{
    struct capture_frame_context *frame_ctx  = data;
    struct ffmpeg_context        *ffmpeg_ctx = &frame_ctx->ffmpeg_ctx;

    if (!frame_ctx->capturing_video) {
        // We need exit here, differently to our video frame handler, since
        // on_process gets continuously requested automatically, and we can't
        // safely stop it from within this handler.
        return;
    }

    struct pw_buffer *pw_buf = pw_stream_dequeue_buffer(m_state.stream);
    if (pw_buf == NULL) {
        eprintf("Pipewire out of buffers\n");
        return;
    }

    struct spa_buffer      *spa_buf     = pw_buf->buffer;
    struct spa_meta_header *meta_header = spa_buffer_find_meta_data(spa_buf, SPA_META_Header, sizeof(*meta_header));

    // XXX TODO: We obviously don't need this exact f32 format for sizeof(float)
    // to be appropriate. Improve this assert once we don't hardcode the format
    // anymore.
    assert(  m_state.format    == SPA_AUDIO_FORMAT_F32P);
    uint32_t bytes_per_sample   = sizeof(float);
    uint32_t n_samples          = spa_buf->datas[0].chunk->size / bytes_per_sample;
    // NOTE: n_samples_leftover must be adjusted if moved to after av_audio_fifo_write()
    int      n_samples_leftover = av_audio_fifo_size(ffmpeg_ctx->av_audio_fifo);
    // XXX TODO: FR/FL should be mapped to 0/1 dynamically.
    void    *spa_buf_planes[SCRAN_PIPEWIRE_N_CHANNELS];

    for (int i = 0; i < SCRAN_PIPEWIRE_N_CHANNELS; ++i) {
        float *samples = spa_buf->datas[i].data;
        if (samples == NULL) {
            goto cont;
        }
        spa_buf_planes[i] = samples;
    }
    av_audio_fifo_write(ffmpeg_ctx->av_audio_fifo, spa_buf_planes, n_samples);


    int64_t pts_incoming    = (meta_header != NULL) ? meta_header->pts : pw_buf->time;
    int64_t pts_fifo_start  = pts_incoming
                              - frame_ctx->presentation_time_nsec_start
                              - av_rescale(n_samples_leftover, NSEC_PER_SEC, SCRAN_PIPEWIRE_SAMPLE_RATE);
    int      frame_size     = ffmpeg_ctx->av_codec_ctx_audio->frame_size;

    assert(frame_size == ffmpeg_ctx->av_frame_captured_audio->nb_samples);

    int64_t pts_curr = pts_fifo_start;
    while (av_audio_fifo_size(ffmpeg_ctx->av_audio_fifo) >= frame_size) {
        av_audio_fifo_read(
            ffmpeg_ctx->av_audio_fifo,
            (void **)ffmpeg_ctx->av_frame_captured_audio->data,
            frame_size
        );

        ffmpeg_ctx->av_frame_captured_audio->pts = pts_curr;

        int ret_enc = avcodec_send_frame(ffmpeg_ctx->av_codec_ctx_audio, ffmpeg_ctx->av_frame_captured_audio);
        if (ret_enc < 0) {
            eprintf("Error while sending audio frame\n");
            goto cont; // TODO: goto err?
        }

        while (ret_enc >= 0) {
            ret_enc = avcodec_receive_packet(ffmpeg_ctx->av_codec_ctx_audio, ffmpeg_ctx->av_packet_audio);

            if (ret_enc == AVERROR_EOF || ret_enc == AVERROR(EAGAIN)) {
                break;
            } else if (ret_enc < 0) {
                eprintf("Error while encoding audio frame\n");
                goto cont; // TODO: goto err?
            }

            write_audio_packet(frame_ctx, ffmpeg_ctx->av_packet_audio);
        }

        pts_curr += av_rescale(frame_size, NSEC_PER_SEC, SCRAN_PIPEWIRE_SAMPLE_RATE);
    }

    av_packet_unref(ffmpeg_ctx->av_packet_audio);

cont:
    pw_stream_queue_buffer(m_state.stream, pw_buf);
    return;
}


static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
    // TODO: handle param_changed ?
};


// Deinit the pipewire listeners etc. without full pipewire deinit.
// Must still call scran_pipewire_init() to restart again.
void
scran_pipewire_reset()
{
    if (m_state.stream != NULL) {
        pw_stream_disconnect(m_state.stream);
        spa_hook_remove(&m_state.stream_listener);
        pw_stream_destroy(m_state.stream);
        m_state.stream  = NULL;
    }

    if (m_state.core != NULL) {
        pw_core_disconnect(m_state.core);
        m_state.core = NULL;
    }

    if (m_state.ctx != NULL) {
        pw_context_destroy(m_state.ctx);
        m_state.ctx = NULL;
    }

    if (m_state.loop_fd != -1) {
        epoll_ctl(m_state.epoll_fd, EPOLL_CTL_DEL, m_state.loop_fd, NULL);
        m_state.loop_fd  = -1;
    }

    if (m_state.loop != NULL) {
        pw_loop_leave(m_state.loop);
        pw_loop_destroy(m_state.loop);
        m_state.loop  = NULL;
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
}

void
scran_pipewire_pre_init(int epoll_fd)
{
    m_state.epoll_fd = epoll_fd;
}

// NOTE: scran_pipewire_pre_init() must be called first to set epoll fd
//
// TODO: Error-checking and destroy on failed init
bool
scran_pipewire_init(
    struct capture_frame_context *frame_ctx,
    enum spa_audio_format format
) {
    DEBUG("scran_pipewire_init()\n");

    if (m_state.pw_inited == false) {
        pw_init(NULL, NULL);
        m_state.pw_inited = true;
    }

    m_state.loop    = pw_loop_new(NULL);
    if (m_state.loop == NULL) {
        eprintf("WARNING: Failed to create PipeWire loop\n");
        return false;
    }
    m_state.loop_fd = pw_loop_get_fd(m_state.loop);

    assert(m_state.epoll_fd != -1);
    struct epoll_event epoll_event = {
        .events = EPOLLIN,
        .data.fd = m_state.loop_fd
    };
    epoll_ctl(m_state.epoll_fd, EPOLL_CTL_ADD, m_state.loop_fd, &epoll_event);

    m_state.ctx  = pw_context_new(    m_state.loop, NULL, 0);
    if (m_state.ctx == NULL) {
        eprintf("WARNING: Failed to create PipeWire context\n");
        return false;
    }

    m_state.core = pw_context_connect(m_state.ctx , NULL, 0);
    if (m_state.core == NULL) {
        eprintf("WARNING: Failed to connect to PipeWire daemon\n");
        return false;
    }

    // NOTE: pw_stream takes ownership of this. Don't free.
    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE         , "Audio"  ,
        PW_KEY_MEDIA_CATEGORY     , "Capture",
        PW_KEY_MEDIA_ROLE         , "Screen" ,
        PW_KEY_STREAM_CAPTURE_SINK, "true"   ,
        NULL
    );
    m_state.stream = pw_stream_new(m_state.core, "scran-audio-capture", props);

    m_state.userdata = frame_ctx;
    m_state.format = format;

    return true;
}

bool
scran_pipewire_connect()
{
    pw_loop_enter(m_state.loop);
    pw_stream_add_listener(m_state.stream, &m_state.stream_listener, &stream_events, m_state.userdata);

    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[] =  {
        spa_format_audio_raw_build(
            &builder,
            SPA_PARAM_EnumFormat,
            &(struct spa_audio_info_raw){
                .format   = m_state.format,
                .rate     = SCRAN_PIPEWIRE_SAMPLE_RATE,
                .channels = SCRAN_PIPEWIRE_N_CHANNELS
            }
        )
    };

    if (0 > pw_stream_connect(
                m_state.stream,
                SPA_DIRECTION_INPUT,
                PW_ID_ANY,
                PW_STREAM_FLAG_AUTOCONNECT
                | PW_STREAM_FLAG_MAP_BUFFERS,
                params,
                sizeof(params) / sizeof(params[0]))
    ) {
        eprintf("Error: Failed to connect pipewire stream (%d: %s).\n", errno, strerror(errno));
        return false;
    }

    DEBUG("Pipewire stream connected.\n");

    return true;
}


bool
scran_pipewire_update(int fd_ready)
{
    if (fd_ready != m_state.loop_fd) {
        return true;
    }

    assert(m_state.loop != NULL);

    if (0 > pw_loop_iterate(m_state.loop, 0)) {
        eprintf("Error: Failed to iterate pipewire loop\n");
        return false;
    }

    return true;
}

