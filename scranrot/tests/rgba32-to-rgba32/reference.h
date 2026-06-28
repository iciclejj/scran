#ifndef SCRANROT_TEST_REFERENCE_RGBA32_TO_RGBA32_H
#define SCRANROT_TEST_REFERENCE_RGBA32_TO_RGBA32_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "scranrot.h"

#include "../test-types.h"

static inline bool
scranrot_test_reference_transform_is_supported(enum scranrot_transform transform)
{
    switch (transform) {
    case SCRANROT_TRANSFORM_270:
    case SCRANROT_TRANSFORM_90:
    case SCRANROT_TRANSFORM_180:
    case SCRANROT_TRANSFORM_NORMAL:
        return true;
    case SCRANROT_TRANSFORM_FLIPPED_270:
    case SCRANROT_TRANSFORM_FLIPPED_90:
    case SCRANROT_TRANSFORM_FLIPPED_180:
    case SCRANROT_TRANSFORM_FLIPPED:
    default:
        return false;
    }
}

static inline struct scranrot_test_dimensions
scranrot_test_get_reference_dst_dimensions(
    const struct scranrot_test_dimensions src_dims,
    enum scranrot_transform transform
) {
    int dst_width_px;
    int dst_height_px;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
    case SCRANROT_TRANSFORM_FLIPPED_270:
    case SCRANROT_TRANSFORM_90:
    case SCRANROT_TRANSFORM_FLIPPED_90:
        dst_width_px  = src_dims.height_px;
        dst_height_px = src_dims.width_px;
        break;
    case SCRANROT_TRANSFORM_180:
    case SCRANROT_TRANSFORM_FLIPPED_180:
    case SCRANROT_TRANSFORM_NORMAL:
    case SCRANROT_TRANSFORM_FLIPPED:
    default:
        dst_width_px  = src_dims.width_px;
        dst_height_px = src_dims.height_px;
        break;
    }

    return (struct scranrot_test_dimensions){
        .width_px  = dst_width_px,
        .height_px = dst_height_px,
    };
}

static inline bool
scranrot_test_reference_rgba_shuffle_is_valid(uint32_t rgba_shuffle)
{
    return ((rgba_shuffle >>  0) & 0xff) < RGBA32_PIXEL_STRIDE
        && ((rgba_shuffle >>  8) & 0xff) < RGBA32_PIXEL_STRIDE
        && ((rgba_shuffle >> 16) & 0xff) < RGBA32_PIXEL_STRIDE
        && ((rgba_shuffle >> 24) & 0xff) < RGBA32_PIXEL_STRIDE;
}

static inline bool
scranrot_test_reference_rgba32_to_rgba32(
    const uint8_t          *src,
    int                     src_width_px,
    int                     src_height_px,
    int                     src_stride_bytes,
    uint8_t                *dst,
    uint32_t                rgba_shuffle,
    enum scranrot_transform transform,
    uintptr_t              *dst_stride
) {
    const struct scranrot_test_dimensions dst_dimensions = scranrot_test_get_reference_dst_dimensions(
        (struct scranrot_test_dimensions){
            .width_px  = src_width_px,
            .height_px = src_height_px
        },
        transform
    );

    if (!scranrot_test_reference_transform_is_supported(transform)) {
        return false;
    }
    if (!scranrot_test_reference_rgba_shuffle_is_valid(rgba_shuffle)) {
        return false;
    }

    const int dst_stride_bytes = dst_dimensions.width_px * RGBA32_PIXEL_STRIDE;

    for (int y_src = 0; y_src < src_height_px; ++y_src) {
        for (int x_src = 0; x_src < src_width_px; ++x_src) {

            int x_dst;
            int y_dst;

            switch (transform) {
            case SCRANROT_TRANSFORM_270:
                x_dst = y_src;
                y_dst = (src_width_px - 1) - x_src;
                break;
            case SCRANROT_TRANSFORM_180:
                x_dst = (src_width_px  - 1) - x_src;
                y_dst = (src_height_px - 1) - y_src;
                break;
            case SCRANROT_TRANSFORM_90:
                x_dst = (src_height_px - 1) - y_src;
                y_dst = x_src;
                break;
            case SCRANROT_TRANSFORM_NORMAL:
                x_dst = x_src;
                y_dst = y_src;
                break;
            default:
                assert(false && "  unsupported transform");
                return false;
            }

            uint8_t const *const src_pixel = src + (y_src * src_stride_bytes) + (x_src * RGBA32_PIXEL_STRIDE);
            uint8_t *const       dst_pixel = dst + (y_dst * dst_stride_bytes) + (x_dst * RGBA32_PIXEL_STRIDE);

            dst_pixel[0] = src_pixel[(rgba_shuffle >> 0) & 0xff];
            dst_pixel[1] = src_pixel[(rgba_shuffle >> 8) & 0xff];
            dst_pixel[2] = src_pixel[(rgba_shuffle >> 16) & 0xff];
            dst_pixel[3] = src_pixel[(rgba_shuffle >> 24) & 0xff];
        }
    }

    *dst_stride = (uintptr_t)dst_stride_bytes;

    return true;
}

#endif
