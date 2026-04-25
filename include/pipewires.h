#ifndef SCRAN_PIPEWIRES_H
#define SCRAN_PIPEWIRES_H


#include <pipewire/pipewire.h>
#include <spa/param/format-types.h>

#include "state.h"


// TODO: Use struct spa_audio_info_raw to set this dynamically or to
// let e.g. init_ffmpeg() decide.
#define SCRAN_PIPEWIRE_N_CHANNELS 2
#define SCRAN_PIPEWIRE_SAMPLE_RATE 48000


void scran_pipewire_pre_init(int epoll_fd);
 bool scran_pipewire_init(struct capture_frame_context *frame_ctx, enum spa_audio_format format);
 void scran_pipewire_reset();
 void scran_pipewire_destroy();
bool scran_pipewire_update( int fd_ready);
bool scran_pipewire_connect();


#endif
