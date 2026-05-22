#include <stddef.h>

#include "../include/scranrot.h"
#include "../include/scranrot-util.h"
#include "./generic.h"


SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
static inline uint32_t
_fallback_convert_pixel_format(
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


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_270(
    const void *const restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    uint32_t rgba32_shift_mask = *(uint32_t *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    const int dst_y_px_max = src_width_px - 1;

    static const int tile_height = SCRANROT_FALLBACK_STRIDE_PX;
    // XXX: Keeping tile_width for testing despite optimal seems to be 1
    //      (on a 5600h, both with and without auto-vectorization)
    static const int tile_width = 1;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const char *const _src = (char *)src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    // NOTE: Rotation-specific
                    char *const _dst = (char *)dst
                        + (y + _y) * RGBA32_PIXEL_STRIDE
                        + (dst_y_px_max - (x + _x)) * dst_stride_bytes;

                    uint32_t val = *(uint32_t *)_src;
                    val = _fallback_convert_pixel_format(val, rgba32_shift_mask);

                    *(uint32_t *)_dst = val;
                }
            }

        }
    }


}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_180(
    const void *const restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    uint32_t rgba32_shift_mask = *(uint32_t *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    static const int tile_height = SCRANROT_FALLBACK_STRIDE_PX;
    // XXX: Keeping tile_width for testing despite optimal seems to be 1
    //      (on a 5600h, both with and without auto-vectorization)
    static const int tile_width = 1;

    // XXX: This assumes a tile_width of 1 (doesn't ensure padding/alignment)
    char *const dst_last_pixel_address =
        (char *)dst
        + (src_height_px - 1) * dst_stride_bytes
        + (src_width_px - 1) * RGBA32_PIXEL_STRIDE
        ;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const char *const _src = (char *)src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    char *const _dst = (char *)dst_last_pixel_address
                        - (y + _y) * dst_stride_bytes
                        - (x + _x) * RGBA32_PIXEL_STRIDE;

                    uint32_t val = *(uint32_t *)_src;
                    val = _fallback_convert_pixel_format(val, rgba32_shift_mask);

                    *(uint32_t *)_dst = val;
                }
            }

        }
    }
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_90(
    const void *const restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    uint32_t rgba32_shift_mask = *(uint32_t *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    const int dst_x_px_max = src_height_px - 1;

    static const int tile_height = SCRANROT_FALLBACK_STRIDE_PX;
    // XXX: Keeping tile_width for testing despite optimal seems to be 1
    //      (on a 5600h, both with and without auto-vectorization)
    static const int tile_width = 1;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const char *const _src = (char *)src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    // NOTE: Rotation-specific (90 vs 270)
                    char *const _dst = (char *)dst
                        + (dst_x_px_max - (y + _y)) * RGBA32_PIXEL_STRIDE
                        + (x + _x) * dst_stride_bytes;

                    uint32_t val = *(uint32_t *)_src;
                    val = _fallback_convert_pixel_format(val, rgba32_shift_mask);

                    *(uint32_t *)_dst = val;
                }
            }

        }
    }
}

SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer__fallback__rotate_0(
    const void *const restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shift_mask // Mask for _mm_shuffle_epi8
) {
    uint32_t rgba32_shift_mask = *(uint32_t *)_rgba32_shift_mask; // Mask for _mm_shuffle_epi8

    static const int tile_height = SCRANROT_FALLBACK_STRIDE_PX;
    // XXX: Keeping tile_width for testing despite optimal seems to be 1
    //      (on a 5600h, both with and without auto-vectorization)
    static const int tile_width = 1;

    for (int y = 0; y < src_height_px; y += tile_height) {
        for (int x = 0; x < src_width_px; x += tile_width) {

            for (int _y = 0; _y < tile_height; ++_y) {
                for (int _x = 0; _x < tile_width; ++_x) {

                    const char *const _src = (char *)src
                        + (y + _y) * src_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    char *const _dst = (char *)dst
                        + (y + _y) * dst_stride_bytes
                        + (x + _x) * RGBA32_PIXEL_STRIDE;

                    uint32_t val = *(uint32_t *)_src;
                    val = _fallback_convert_pixel_format(val, rgba32_shift_mask);

                    *(uint32_t *)_dst = val;
                }
            }

        }
    }
}


bool
scranrot_transform_framebuffer_fallback(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t _rgba_shuffle_mask,
    enum scranrot_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {
    // XXX TODO(!!): IMPLEMENT THIS!!
    bool dimensions_supported = src_width_px >= SCRANROT_FALLBACK_STRIDE_PX && src_height_px >= SCRANROT_FALLBACK_STRIDE_PX;
    if (!dimensions_supported) {
        return false;
    }


    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)

    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = scranrot_get_transformed_width(src_width_px, src_height_px, transform);
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;
    *dst_with_offset = dst;

    const uint32_t rgba_shift_mask = _rgba_shuffle_mask * 8;

    scranrot_transform_framebuffer_impl_fn transform_fn = NULL;

    // XXX TODO: Implement flipped
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
        src,
        src_width_px,
        src_height_px,
        src_stride_bytes,
        dst,
        dst_stride_bytes,

        transform_fn,
        transform,
        &rgba_shift_mask,
        SCRANROT_FALLBACK_STRIDE_PX,
        SCRANROT_FALLBACK_STRIDE_PX
    );
}
