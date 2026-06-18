#include "scranrot.h"
#include "../util.hpp"
#include "../generic-dispatch.hpp"
#include "../types.hpp"

using namespace scranrot::internal;


enum {
    KERNEL_TILE_WIDTH_PX  = 1, // Optimal seems to be 1 (on a 5600h, both with and without auto-vectorization)
    KERNEL_TILE_HEIGHT_PX = 4,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};


SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
static inline u32
convert_pixel_format(
    u32 pixel,
    u32 rgba_shift_mask // NOTE: NOT Shuffle mask.
) {
    u32 converted_pixel =
             ((pixel & 0xFF000000) >> 24) << ((rgba_shift_mask & 0xFF000000) >> 24)
           | ((pixel & 0x00FF0000) >> 16) << ((rgba_shift_mask & 0x00FF0000) >> 16)
           | ((pixel & 0x0000FF00) >> 8)  << ((rgba_shift_mask & 0x0000FF00) >> 8)
           | ((pixel & 0x000000FF))       << ((rgba_shift_mask & 0x000000FF))
    ;

    return converted_pixel;
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_270(
    const u8 *const __restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    u8 *const __restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    u32 rgba32_shift_mask = *(u32 *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    static_assert(KERNEL_TILE_HEIGHT_PX == 4, "270 kernel assumes 4-row RGBA32 tile");

    const int dst_y_px_max = src_width_px - 1;

    static const int tile_height = 4;
    static const int tile_width  = KERNEL_TILE_WIDTH_PX;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const u8 *const _src = src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    // NOTE: Rotation-specific
                    u8 *const _dst = dst
                        + (y + _y) * RGBA32_PIXEL_STRIDE
                        + (dst_y_px_max - (x + _x)) * dst_stride_bytes;

                    u32 val = *(u32 *)_src;
                    val = convert_pixel_format(val, rgba32_shift_mask);

                    *(u32 *)_dst = val;
                }
            }

        }
    }
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_180(
    const u8 *const __restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    u8 *const __restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    u32 rgba32_shift_mask = *(u32 *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    static_assert(KERNEL_TILE_HEIGHT_PX == 4, "180 kernel assumes 4-row RGBA32 tile");

    static const int tile_height = 4;
    static const int tile_width  = KERNEL_TILE_WIDTH_PX;

    u8 *const dst_last_pixel_address =
        dst
        + (src_height_px - 1) * dst_stride_bytes
        + (src_width_px - 1) * RGBA32_PIXEL_STRIDE
        ;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const u8 *const _src = src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    u8 *const _dst = dst_last_pixel_address
                        - (y + _y) * dst_stride_bytes
                        - (x + _x) * RGBA32_PIXEL_STRIDE;

                    u32 val = *(u32 *)_src;
                    val = convert_pixel_format(val, rgba32_shift_mask);

                    *(u32 *)_dst = val;
                }
            }

        }
    }
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_90(
    const u8 *const __restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    u8 *const __restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    u32 rgba32_shift_mask = *(u32 *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    static_assert(KERNEL_TILE_HEIGHT_PX == 4, "90 kernel assumes 4-row RGBA32 tile");

    const int dst_x_px_max = src_height_px - 1;

    static const int tile_height = 4;
    static const int tile_width  = KERNEL_TILE_WIDTH_PX;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const u8 *const _src = src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    // NOTE: Rotation-specific (90 vs 270)
                    u8 *const _dst = dst
                        + (dst_x_px_max - (y + _y)) * RGBA32_PIXEL_STRIDE
                        + (x + _x) * dst_stride_bytes;

                    u32 val = *(u32 *)_src;
                    val = convert_pixel_format(val, rgba32_shift_mask);

                    *(u32 *)_dst = val;
                }
            }

        }
    }
}

SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_0(
    const u8 *const __restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    u8 *const __restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    u32 rgba32_shift_mask = *(u32 *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    static_assert(KERNEL_TILE_HEIGHT_PX == 4, "0 kernel assumes 4-row RGBA32 tile");

    static const int tile_height = 4;
    static const int tile_width  = KERNEL_TILE_WIDTH_PX;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const u8 *const _src = src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    u8 *const _dst = dst
                        + (y + _y) * dst_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    u32 val = *(u32 *)_src;
                    val = convert_pixel_format(val, rgba32_shift_mask);

                    *(u32 *)_dst = val;
                }
            }

        }
    }
}


bool
scranrot::internal::transform_framebuffer_fallback(
    const u8 *src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    u8 *dst,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    u32 _rgba_shuffle_mask,
    enum scranrot_transform transform,
    uintptr_t *dst_stride
) {
    // XXX TODO(!!): IMPLEMENT THIS!!
    if (SCRANROT_UNLIKELY(src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX)) {
        return false;
    }

    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)

    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = get_transformed_width(src_width_px, src_height_px, transform);
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;

    const u32 rgba_shift_mask = _rgba_shuffle_mask * 8;

    transform_framebuffer_impl_fn transform_fn = nullptr;

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

    SCRANROT_ASSERT(transform_fn != nullptr);
    return transform_framebuffer__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst, dst_stride_bytes,
        transform_fn,
        transform, &rgba_shift_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX
    );
}
