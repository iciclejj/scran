#ifndef SCRAN_PIPEWIRES_H
#define SCRAN_PIPEWIRES_H


#include <pipewire/pipewire.h>
#include <spa/param/format-types.h>

#include "state.h"


// TODO: Use struct spa_audio_info_raw to set this dynamically or to
// let e.g. init_ffmpeg() decide.
#define SCRAN_PIPEWIRE_N_CHANNELS 2
#define SCRAN_PIPEWIRE_SAMPLE_RATE 48000


void scran_pipewire_prepare(int epoll_fd);
bool scran_pipewire_attach(struct scran_output *output, enum spa_audio_format format);
void scran_pipewire_detach(struct scran_output *output);
void scran_pipewire_destroy(void);

bool scran_pipewire_update(int fd_ready);


#endif
