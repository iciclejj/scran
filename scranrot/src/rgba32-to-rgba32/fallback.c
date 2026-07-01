#include <stddef.h>

#include "scranrot.h"
#include "../util.h"
#include "../generic-kernel-dispatcher.h"
#include "../implementations.h"


enum {
    KERNEL_TILE_WIDTH_PX  = 1, // Optimal seems to be 1 (on a 5600h, both with and without auto-vectorization)
    KERNEL_TILE_HEIGHT_PX = 4,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};


static inline uint32_t SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
convert_pixel_format(
    uint32_t pixel,
    uint32_t rgba_shift_mask // NOTE: NOT Shuffle mask.
) {
    uint32_t converted_pixel =
             ((pixel & 0xFF000000) >> 24) << ((rgba_shift_mask & 0xFF000000) >> 24)
           | ((pixel & 0x00FF0000) >> 16) << ((rgba_shift_mask & 0x00FF0000) >> 16)
           | ((pixel & 0x0000FF00) >> 8)  << ((rgba_shift_mask & 0x0000FF00) >> 8)
           | ((pixel & 0x000000FF))       << ((rgba_shift_mask & 0x000000FF))
    ;

    return converted_pixel;
}

static inline uint32_t SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
get_rgba32_shift_mask(uint32_t rgba32_shuffle_mask) {
    return rgba32_shuffle_mask * 8;
}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer__fallback__rotate_270(
    const uint8_t *const restrict src,
    const int src_width_px, // Width of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const uint32_t rgba32_shuffle_mask
) {
    const uint32_t rgba32_shift_mask = get_rgba32_shift_mask(rgba32_shuffle_mask);

    _Static_assert(KERNEL_TILE_HEIGHT_PX == 4, "270 kernel assumes 4-row RGBA32 tile");

    const int dst_height_px = src_width_px;

    static const int tile_height = 4;
    static const int tile_width  = KERNEL_TILE_WIDTH_PX;

    uint8_t const *src_tile_row = src;
    uint8_t       *dst_tile_col = scranrot_rgba32_last_row_start(dst, dst_height_px, dst_stride_bytes);

    for (int y = 0; y < src_height_px; y += tile_height) {
        uint8_t const *src_tile = src_tile_row;
        uint8_t       *dst_tile = dst_tile_col;

        for (int x = 0; x < src_width_px; x += tile_width) {
            uint8_t const *_src_tile_row = src_tile;
            uint8_t       *_dst_tile_col = dst_tile;

            for (int _y = 0; _y < tile_height; ++_y) {
                uint8_t const *_src = _src_tile_row;
                uint8_t       *_dst = _dst_tile_col;

                for (int _x = 0; _x < tile_width; ++_x) {
                    uint32_t val = scranrot_loadu_u32(_src);
                    val = convert_pixel_format(val, rgba32_shift_mask);
                    scranrot_storeu_u32(_dst, val);

                    _src += sizeof(uint32_t);
                    _dst -= dst_stride_bytes;
                }

                _src_tile_row += src_stride_bytes;
                _dst_tile_col += sizeof(uint32_t);
            }

            src_tile += tile_width * sizeof(uint32_t);
            dst_tile -= tile_width * dst_stride_bytes;
        }

        src_tile_row += tile_height * src_stride_bytes;
        dst_tile_col += tile_height * sizeof(uint32_t);
    }

}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer__fallback__rotate_180(
    const uint8_t *const restrict src,
    const int src_width_px, // Width of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const uint32_t rgba32_shuffle_mask
) {
    const uint32_t rgba32_shift_mask = get_rgba32_shift_mask(rgba32_shuffle_mask);

    uint8_t const *src_row = src;
    uint8_t       *dst_row = scranrot_rgba32_last_row_end(dst, src_width_px, src_height_px, dst_stride_bytes)
                             - RGBA32_PIXEL_STRIDE;

    for (int y = 0; y < src_height_px; ++y) {
        uint8_t const *_src = src_row;
        uint8_t       *_dst = dst_row;

        for (int x = 0; x < src_width_px; ++x) {
            uint32_t val = scranrot_loadu_u32(_src);
            val = convert_pixel_format(val, rgba32_shift_mask);
            scranrot_storeu_u32(_dst, val);

            _src += sizeof(uint32_t);
            _dst -= sizeof(uint32_t);
        }

        src_row += src_stride_bytes;
        dst_row -= dst_stride_bytes;
    }
}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer__fallback__rotate_90(
    const uint8_t *const restrict src,
    const int src_width_px, // Width of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const uint32_t rgba32_shuffle_mask
) {
    const uint32_t rgba32_shift_mask = get_rgba32_shift_mask(rgba32_shuffle_mask);

    _Static_assert(KERNEL_TILE_HEIGHT_PX == 4, "90 kernel assumes 4-row RGBA32 tile");

    const int dst_width_px = src_height_px;

    static const int tile_height = 4;
    static const int tile_width  = KERNEL_TILE_WIDTH_PX;

    uint8_t const *src_tile_row = src;
    uint8_t       *dst_tile_col = scranrot_rgba32_row_end(dst, dst_width_px)
                                  - RGBA32_PIXEL_STRIDE;

    for (int y = 0; y < src_height_px; y += tile_height) {
        uint8_t const *src_tile = src_tile_row;
        uint8_t       *dst_tile = dst_tile_col;

        for (int x = 0; x < src_width_px; x += tile_width) {
            uint8_t const *_src_tile_row = src_tile;
            uint8_t       *_dst_tile_col = dst_tile;

            for (int _y = 0; _y < tile_height; ++_y) {
                uint8_t const *_src = _src_tile_row;
                uint8_t       *_dst = _dst_tile_col;

                for (int _x = 0; _x < tile_width; ++_x) {
                    uint32_t val = scranrot_loadu_u32(_src);
                    val = convert_pixel_format(val, rgba32_shift_mask);
                    scranrot_storeu_u32(_dst, val);

                    _src += sizeof(uint32_t);
                    _dst += dst_stride_bytes;
                }

                _src_tile_row += src_stride_bytes;
                _dst_tile_col -= sizeof(uint32_t);
            }

            src_tile += tile_width * sizeof(uint32_t);
            dst_tile += tile_width * dst_stride_bytes;
        }

        src_tile_row += tile_height * src_stride_bytes;
        dst_tile_col -= tile_height * sizeof(uint32_t);
    }

}

static void SCRANROT_TARGET_FALLBACK
transform_framebuffer__fallback__rotate_0(
    const uint8_t *const restrict src,
    const int src_width_px, // Width of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const uint32_t rgba32_shuffle_mask
) {
    const uint32_t rgba32_shift_mask = get_rgba32_shift_mask(rgba32_shuffle_mask);

    uint8_t const *src_row = src;
    uint8_t       *dst_row = dst;

    for (int y = 0; y < src_height_px; ++y) {
        uint8_t const *_src = src_row;
        uint8_t       *_dst = dst_row;

        for (int x = 0; x < src_width_px; ++x) {
            uint32_t val = scranrot_loadu_u32(_src);
            val = convert_pixel_format(val, rgba32_shift_mask);
            scranrot_storeu_u32(_dst, val);

            _src += sizeof(uint32_t);
            _dst += sizeof(uint32_t);
        }

        src_row += src_stride_bytes;
        dst_row += dst_stride_bytes;
    }
}


bool
scranrot_transform_framebuffer_fallback(
    const uint8_t *src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *dst,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    uintptr_t *dst_stride
) {
    // XXX TODO(!!): IMPLEMENT THIS!!
    if (SCRANROT_UNLIKELY(src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX)) {
        return false;
    }

    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)

    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = scranrot_get_transformed_width(src_width_px, src_height_px, transform);
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;

    scranrot_transform_framebuffer_impl_fn transform_fn = NULL;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer__fallback__rotate_270; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer__fallback__rotate_180; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer__fallback__rotate_90;  break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer__fallback__rotate_0;  break;
    default:
        // XXX TODO: Implement flipped
        return false;
    }

    SCRANROT_ASSERT(transform_fn != NULL);
    return transform_framebuffer__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst, dst_stride_bytes,
        transform_fn,
        transform, rgba_shuffle_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX
    );
}
