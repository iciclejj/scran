#include "lib_interop.h"
#include "capture.h"
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

// XXX TODO: Verify that this assignment happens at compile time.
//           Also, maybe make it prettier if possible...
#define _BL_FORMAT_INFO_BYTESWAPPED_RGB_KEPT_ALPHA(_bl_format_info_) \
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
    case WL_SHM_FORMAT_ABGR8888: return _BL_FORMAT_INFO_BYTESWAPPED_RGB_KEPT_ALPHA(bl_format_info[BL_FORMAT_PRGB32]);
    case WL_SHM_FORMAT_XBGR8888: return _BL_FORMAT_INFO_BYTESWAPPED_RGB_KEPT_ALPHA(bl_format_info[BL_FORMAT_XRGB32]);
    // TODO: Consider default: assert(true)?
    default: return (BLFormatInfo){ 0 };
    }
}

uint32_t
wl_shm_format_to_blend2d_scran_rgba32_shuffle(enum wl_shm_format wl_shm_format)
{
    assert(CAPTURE_IMAGE_OUTPUT_BLFORMAT_DEFAULT == BL_FORMAT_PRGB32);
#ifndef NDEBUG
    enum wl_shm_format bl_default_to_wl = WL_SHM_FORMAT_ARGB8888;
    assert(wl_shm_format_to_blend2d(bl_default_to_wl) == CAPTURE_IMAGE_OUTPUT_BLFORMAT_DEFAULT);
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
        case WL_SHM_FORMAT_ARGB8888: return AV_PIX_FMT_ABGR;
        case WL_SHM_FORMAT_XRGB8888: return AV_PIX_FMT_0BGR;
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
