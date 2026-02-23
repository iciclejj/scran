#ifndef LIB_INTEROP_H
#define LIB_INTEROP_H

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/avcodec.h>

#define RGBA32_SHUFFLE_ERROR ((uint32_t)0x00000000)
#define RGBA32_SHUFFLE_NO_CHANGE ((uint32_t)0x03020100)

enum BLFormat wl_shm_format_to_blend2d(enum wl_shm_format wl_shm_format);
struct BLFormatInfo wl_shm_format_to_blend2d_struct(enum wl_shm_format wl_shm_format);

uint32_t wl_shm_format_to_blend2d_scran_rgba32_shuffle(enum wl_shm_format wl_shm_format);

enum AVPixelFormat wl_shm_format_to_ffmpeg(enum wl_shm_format wl_shm_format);
const char * wl_shm_format_to_ffmpeg_cli_str(enum wl_shm_format wl_shm_format);

#endif
