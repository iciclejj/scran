#ifndef LIB_INTEROP_H
#define LIB_INTEROP_H

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/avcodec.h>

#define RGBA32_SHUFFLE_ERROR ((uint32_t)0x00000000)
#define RGBA32_SHUFFLE_NO_CHANGE ((uint32_t)0x03020100)

// XXX: libavfilter doesn't expose the transpose filter's header in the public
// API, for some reason...
enum ScranAVTransposeDir {
    SCRAN_AV_TRANSPOSE_DIR_UNSUPPORTED = -2,
    SCRAN_AV_TRANSPOSE_DIR_NORMAL = -1,

    // Using wayland's naming scheme
    SCRAN_AV_TRANSPOSE_DIR_FLIPPED_90 = 0,
    SCRAN_AV_TRANSPOSE_DIR_270,
    SCRAN_AV_TRANSPOSE_DIR_90,
    SCRAN_AV_TRANSPOSE_DIR_FLIPPED_270,
    SCRAN_AV_TRANSPOSE_DIR_180,
    SCRAN_AV_TRANSPOSE_DIR_FLIPPED,

    // XXX: Not used by wayland. (For future user-configuragable transforms
    // we'll probably use a string filterdescription rather than using this
    // enum directly)
    _SCRAN_AV_TRANSPOSE_FLIPPED_AROUND_HORIZONTAL_AXIS,
};

enum BLFormat wl_shm_format_to_blend2d(enum wl_shm_format wl_shm_format);
struct BLFormatInfo wl_shm_format_to_blend2d_struct(enum wl_shm_format wl_shm_format);

uint32_t wl_shm_format_to_blend2d_scran_rgba32_shuffle(enum wl_shm_format wl_shm_format);

enum AVPixelFormat wl_shm_format_to_ffmpeg(enum wl_shm_format wl_shm_format);
const char * wl_shm_format_to_ffmpeg_cli_str(enum wl_shm_format wl_shm_format);

enum ScranAVTransposeDir wl_output_transform_to_ffmpeg_transpose_dir__inverse(enum wl_output_transform transform);

#endif
