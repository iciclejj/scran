#ifndef SCRAN_LIB_INTEROP_H
#define SCRAN_LIB_INTEROP_H


#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/avcodec.h>
#include <spa/param/audio/raw.h>

#include "scranrot.h"

#define RGBA32_SHUFFLE_ERROR ((uint32_t)0x00000000)
#define RGBA32_SHUFFLE_NO_CHANGE ((uint32_t)0x03020100)

#define ASSERT_SAME_TYPE(a, b) static_assert(_Generic((a), __typeof_unqual__(b): 1, default: 0), "types do not match")


enum BLFormat wl_shm_format_to_blend2d(enum wl_shm_format wl_shm_format);
struct BLFormatInfo wl_shm_format_to_blend2d_struct(enum wl_shm_format wl_shm_format);

uint32_t wl_shm_format_to_blend2d_scranrot_rgba32_shuffle(enum wl_shm_format wl_shm_format);
uint32_t wl_shm_format_to_scranrot_yuv_rgba32_shuffle(enum wl_shm_format wl_shm_format);

enum AVPixelFormat wl_shm_format_to_ffmpeg(enum wl_shm_format wl_shm_format);
const char * wl_shm_format_to_ffmpeg_cli_str(enum wl_shm_format wl_shm_format);

enum spa_audio_format ffmpeg_sample_format_to_pipewire(enum AVSampleFormat format);

static inline enum scranrot_transform
wl_output_transform_to_scranrot(enum wl_output_transform transform) {
    return (enum scranrot_transform)transform;
}


#endif
