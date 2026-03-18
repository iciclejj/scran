#include <assert.h>

#include "../include/scranrot.h"
#include "../include/scranrot-util.h"


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
void
scranrot_transform_framebuffer_fallback(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {
    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)

    assert(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = scranrot_get_transformed_width(src_width_px, src_height_px, transform);
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;
    *dst_with_offset = dst;

    const int dst_height_px = scranrot_get_transformed_height(src_width_px, src_height_px, transform);
    const int dst_width_px = scranrot_get_transformed_width(src_width_px, src_height_px, transform);

    const uint32_t rgba_shift_mask = rgba_shuffle_mask * 8;

    // XXX TODO: Implement flipped
    switch (transform) {
    case SCRANROT_TRANSFORM_FLIPPED:
    case SCRANROT_TRANSFORM_NORMAL:
        {
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
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }
        }
        break;
    case SCRANROT_TRANSFORM_FLIPPED_180:
    case SCRANROT_TRANSFORM_180:
        {
            static const int tile_height = SCRANROT_FALLBACK_STRIDE_PX;
            // XXX: Keeping tile_width for testing despite optimal seems to be 1
            //      (on a 5600h, both with and without auto-vectorization)
            static const int tile_width = 1;

            // XXX: This assumes a tile_width of 1 (doesn't ensure padding/alignment)
            char *const dst_last_pixel_address =
                (char *)dst
                + (dst_height_px - 1) * dst_stride_bytes
                + (dst_width_px - 1) * RGBA32_PIXEL_STRIDE
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
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }
        }
        break;
    case SCRANROT_TRANSFORM_FLIPPED_90:
    case SCRANROT_TRANSFORM_90:
        {
            const int dst_x_px_max = dst_width_px - 1;

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
                                         + dst_x_px_max - (y + _y) * RGBA32_PIXEL_STRIDE
                                         + (x + _x) * dst_stride_bytes;

                            uint32_t val = *(uint32_t *)_src;
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }
        }
        break;
    case SCRANROT_TRANSFORM_FLIPPED_270:
    case SCRANROT_TRANSFORM_270:
        {
            const int dst_y_px_max = dst_height_px - 1;

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
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }

        }
        break;
    }
}
