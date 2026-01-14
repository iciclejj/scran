#ifndef LIB_INTEROP_H
#define LIB_INTEROP_H

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/avcodec.h>

enum BLFormat wl_shm_format_to_blend2d(enum wl_shm_format wl_shm_format);
enum AVPixelFormat wl_shm_format_to_ffmpeg(enum wl_shm_format wl_shm_format);
const char * wl_shm_format_to_ffmpeg_cli_str(enum wl_shm_format wl_shm_format);

#endif
