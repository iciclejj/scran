#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

#include "util/lib-interop.h"
#include "capture.h"
#include "spa/param/audio/raw.h"


// XXX TODO: Verify that this assignment happens at compile time.
//           Also, maybe make it prettier if possible...
#define BL_FORMAT_INFO_BYTESWAPPED_RGB_KEPT_ALPHA(_bl_format_info_) \
    (BLFormatInfo){     \
        .depth = _bl_format_info_.depth,    \
        .flags = _bl_format_info_.flags,    \
        .r_size = _bl_format_info_.r_size,  \
        .g_size = _bl_format_info_.g_size,  \
        .b_size = _bl_format_info_.b_size,  \
        .a_size = _bl_format_info_.a_size,  \
        .r_shift = _bl_format_info_.b_shift,    \
        .g_shift = _bl_format_info_.g_shift,    \
        .b_shift = _bl_format_info_.r_shift,    \
        .a_shift = _bl_format_info_.a_shift,    \
    };

// XXX TODO: Figure out how all the libraries treat rgba vs bgra etc. wrt. endianness
//             - Especially wayland
//             - F.ex. ffmpeg AV_PIX_FMT_RGB32 is endian-dependent
//                   (according to libav comments. not tested yet.)
//             - This line seems to give correct colors:
//                   case WL_SHM_FORMAT_XBGR8888: return AV_PIX_FMT_RGB0;
//               At least when comparing to session::shm_format's output.


// Natively supported formats (index into bl_image_format global)
enum BLFormat
wl_shm_format_to_blend2d(enum wl_shm_format wl_shm_format)
{
    switch (wl_shm_format) {
        case WL_SHM_FORMAT_ARGB8888: return BL_FORMAT_PRGB32;
        case WL_SHM_FORMAT_XRGB8888: return BL_FORMAT_XRGB32;
        case WL_SHM_FORMAT_C8:       return BL_FORMAT_A8;
        // TODO: Consider default: assert(true)?
        default:                     return BL_FORMAT_NONE;
    }
}


struct BLFormatInfo
wl_shm_format_to_blend2d_struct(enum wl_shm_format wl_shm_format)
{
    switch (wl_shm_format) {
    // Builtin (many Blend2D support this directly without a separate conversion step)
    // NOTE: Can convert wl_shm_format builtin enum values using wl_shm_format_to_blend2d
    case WL_SHM_FORMAT_ARGB8888: return bl_format_info[BL_FORMAT_PRGB32];
    case WL_SHM_FORMAT_XRGB8888: return bl_format_info[BL_FORMAT_XRGB32];
    case WL_SHM_FORMAT_C8:       return bl_format_info[BL_FORMAT_A8];
    // Custom
    case WL_SHM_FORMAT_ABGR8888: return BL_FORMAT_INFO_BYTESWAPPED_RGB_KEPT_ALPHA(bl_format_info[BL_FORMAT_PRGB32]);
    case WL_SHM_FORMAT_XBGR8888: return BL_FORMAT_INFO_BYTESWAPPED_RGB_KEPT_ALPHA(bl_format_info[BL_FORMAT_XRGB32]);
    // TODO: Consider default: assert(true)?
    default: return (BLFormatInfo){ 0 };
    }
}


// Scranrot expects a specific pixel layout for its yuv pipeline.
// This creates the shuffle that needs to happen to conform to that layout.
// XXX TODO: Make this clearer or move responsibility for this into scranrot
uint32_t
wl_shm_format_to_scranrot_yuv_rgba32_shuffle(enum wl_shm_format wl_shm_format)
{
    // TODO: Double-check all of these
    switch (wl_shm_format) {
        case WL_SHM_FORMAT_ARGB8888: return 0x03000102;
        case WL_SHM_FORMAT_XRGB8888: return 0x03000102;

        case WL_SHM_FORMAT_ABGR8888: return RGBA32_SHUFFLE_NO_CHANGE;
        case WL_SHM_FORMAT_XBGR8888: return RGBA32_SHUFFLE_NO_CHANGE;

        case WL_SHM_FORMAT_RGBX8888: return 0x00010203;
        case WL_SHM_FORMAT_RGBA8888: return 0x00010203;

        default: return RGBA32_SHUFFLE_ERROR;
    }
}


uint32_t
wl_shm_format_to_blend2d_scranrot_rgba32_shuffle(enum wl_shm_format wl_shm_format)
{
    assert(IMAGE_CAPTURE_OUTPUT_BLFORMAT_DEFAULT == BL_FORMAT_PRGB32);
#ifndef NDEBUG
    enum wl_shm_format bl_default_to_wl = WL_SHM_FORMAT_ARGB8888;
    assert(wl_shm_format_to_blend2d(bl_default_to_wl) == IMAGE_CAPTURE_OUTPUT_BLFORMAT_DEFAULT);
#endif

    switch (wl_shm_format) {
        // XXX: These are the native formats that we check for above. Putting
        // them here for completeness.
        case WL_SHM_FORMAT_ARGB8888: return 0x03020100;
        case WL_SHM_FORMAT_XRGB8888: return 0x03020100; // TODO: Verify this will be fine

        case WL_SHM_FORMAT_ABGR8888: return 0x03000102;
        case WL_SHM_FORMAT_XBGR8888: return 0x03000102;

        case WL_SHM_FORMAT_RGBX8888: return 0x02010003;
        case WL_SHM_FORMAT_RGBA8888: return 0x02010003;

        default: return RGBA32_SHUFFLE_ERROR;
    }
}


enum AVPixelFormat
wl_shm_format_to_ffmpeg(enum wl_shm_format wl_shm_format)
{
    switch (wl_shm_format) {
        // XXX: Double-check this behavior (at least with session::shm_format)
        //      See TODO at the top.
        case WL_SHM_FORMAT_ARGB8888: return AV_PIX_FMT_BGRA;
        case WL_SHM_FORMAT_XRGB8888: return AV_PIX_FMT_BGR0;
        case WL_SHM_FORMAT_ABGR8888: return AV_PIX_FMT_RGBA;
        case WL_SHM_FORMAT_XBGR8888: return AV_PIX_FMT_RGB0;
        case WL_SHM_FORMAT_C8:       return AV_PIX_FMT_GRAY8;
        // TODO: Consider default: assert(true)?
        default:                     return AV_PIX_FMT_NONE;
    }
}


const char *
wl_shm_format_to_ffmpeg_cli_str(enum wl_shm_format wl_shm_format)
{
    return av_get_pix_fmt_name(wl_shm_format_to_ffmpeg(wl_shm_format));
}


enum spa_audio_format
ffmpeg_sample_format_to_pipewire(enum AVSampleFormat format)
{
    // XXX TODO: Revisit the SPA_AUDIO_FORMAT_UNKNOWN assignments once they
    // might actually become relevant/selected.

    switch (format) {
        case AV_SAMPLE_FMT_NONE: return SPA_AUDIO_FORMAT_UNKNOWN;

        case AV_SAMPLE_FMT_U8:   return SPA_AUDIO_FORMAT_U8;
        case AV_SAMPLE_FMT_S16:  return SPA_AUDIO_FORMAT_S16;
        case AV_SAMPLE_FMT_S32:  return SPA_AUDIO_FORMAT_S32;
        case AV_SAMPLE_FMT_FLT:  return SPA_AUDIO_FORMAT_F32;
        case AV_SAMPLE_FMT_DBL:  return SPA_AUDIO_FORMAT_F64;

        case AV_SAMPLE_FMT_U8P:  return SPA_AUDIO_FORMAT_U8P;
        case AV_SAMPLE_FMT_S16P: return SPA_AUDIO_FORMAT_S16P;
        case AV_SAMPLE_FMT_S32P: return SPA_AUDIO_FORMAT_S32P;
        case AV_SAMPLE_FMT_FLTP: return SPA_AUDIO_FORMAT_F32P;
        case AV_SAMPLE_FMT_DBLP: return SPA_AUDIO_FORMAT_F64P;
        case AV_SAMPLE_FMT_S64:  return SPA_AUDIO_FORMAT_UNKNOWN;
        case AV_SAMPLE_FMT_S64P: return SPA_AUDIO_FORMAT_UNKNOWN;

        default:                 return SPA_AUDIO_FORMAT_UNKNOWN;
    }
}

