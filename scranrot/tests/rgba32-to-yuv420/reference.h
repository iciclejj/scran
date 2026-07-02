#ifndef SCRANROT_TEST_REFERENCE_RGBA32_TO_YUV420_H
#define SCRANROT_TEST_REFERENCE_RGBA32_TO_YUV420_H


#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "scranrot.h"
#include "../rgba32-to-rgba32/reference.h"
#include "../test-util.h"


// Full-range BT.709 Y'CbCr reference conversion.
//
// The coefficients below are exact rational forms of:
//   Y  =  0.2126 R + 0.7152 G + 0.0722 B
//   Cb = (B - Y) / (2 - 2 * 0.0722) + 128
//   Cr = (R - Y) / (2 - 2 * 0.2126) + 128
//
// Chroma is computed from the average RGB value of each 2x2 YUV420 block,
// then rounded to nearest integer.
static inline uint8_t
reference_yuv_clamp_to_u8(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > UINT8_MAX) {
        return UINT8_MAX;
    }
    return (uint8_t)value;
}

static inline uint8_t
reference_rgba32_to_yuv_y(int r, int g, int b) {
    return reference_yuv_clamp_to_u8(
        (2126 * r + 7152 * g + 722 * b + 5000) / 10000
    );
}
static inline uint8_t
reference_rgba32_to_yuv_u(int sum_r, int sum_g, int sum_b) {
    enum { DENOMINATOR = 18556 * 4 };
    return reference_yuv_clamp_to_u8(
        (128 * DENOMINATOR - 2126 * sum_r - 7152 * sum_g + 9278 * sum_b + DENOMINATOR / 2) / DENOMINATOR
    );
}
static inline uint8_t
reference_rgba32_to_yuv_v(int sum_r, int sum_g, int sum_b) {
    enum { DENOMINATOR = 15748 * 4 };
    return reference_yuv_clamp_to_u8(
        (128 * DENOMINATOR + 7874 * sum_r - 7152 * sum_g - 722 * sum_b + DENOMINATOR / 2) / DENOMINATOR
    );
}

static inline int
reference_y_width(const struct scranrot_test_dimensions dst_dimensions) {
    return dst_dimensions.width_px;
}
static inline int
reference_y_height(const struct scranrot_test_dimensions dst_dimensions) {
    return dst_dimensions.height_px;
}
static inline int
reference_uv_width(const struct scranrot_test_dimensions dst_dimensions) {
    return dst_dimensions.width_px / 2;
}
static inline int
reference_uv_height(const struct scranrot_test_dimensions dst_dimensions) {
    return dst_dimensions.height_px / 2;
}

static inline int
reference_y_stride(const struct scranrot_test_dimensions dst_dimensions) {
    return reference_y_width(dst_dimensions);
}
static inline int
reference_uv_stride(const struct scranrot_test_dimensions dst_dimensions) {
    return reference_uv_width(dst_dimensions);
}

static inline size_t
reference_y_size(const struct scranrot_test_dimensions dst_dimensions, int y_stride) {
    assert(reference_y_width(dst_dimensions) <= y_stride);
    return y_stride * (size_t)reference_y_height(dst_dimensions);
}
static inline size_t
reference_uv_size(const struct scranrot_test_dimensions dst_dimensions, int uv_stride) {
    assert(reference_uv_width(dst_dimensions) <= uv_stride);
    return uv_stride * (size_t)reference_uv_height(dst_dimensions);
}

static inline bool
scranrot_test_reference_rgba32_to_yuv420(
    const uint8_t          *src,
    int                     src_width_px,
    int                     src_height_px,
    int                     src_stride_bytes,
    uint8_t                *dst,
    uint32_t                rgba_shuffle,
    enum scranrot_transform transform,
    struct scranrot_test_yuv420_planes *dst_planes
) {
    assert(dst_planes);

    const struct scranrot_test_dimensions dst_dimensions = scranrot_test_get_reference_dst_dimensions(
        (struct scranrot_test_dimensions){
            .width_px  = src_width_px,
            .height_px = src_height_px
        },
        transform
    );

    const bool src_has_odd_dimensions = (src_width_px & 1) || (src_height_px & 1);
    if (src_has_odd_dimensions) {
        return false;
    }
    const bool dst_has_odd_dimensions = (dst_dimensions.width_px & 1) || (dst_dimensions.height_px & 1);
    if (dst_has_odd_dimensions) {
        return false;
    }

    if (!scranrot_test_reference_transform_is_supported(transform)) {
        return false;
    }

    // Pre-transform dst using the rgba32_to_rgba32 reference function, so that
    // we only need to implement no-transform YUV reference code.
    const size_t   dst_pre_transformed_size   = (size_t)dst_dimensions.width_px * (size_t)dst_dimensions.height_px * RGBA32_PIXEL_STRIDE;
    uint8_t *const dst_pre_transformed = scranrot_test_xmalloc(dst_pre_transformed_size);
    uintptr_t      dst_pre_transformed_stride = 0;
    if (!scranrot_test_reference_rgba32_to_rgba32(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst_pre_transformed,
            rgba_shuffle, transform,
            &dst_pre_transformed_stride)
    ) {
        free(dst_pre_transformed);
        return false;
    }

    const int      y_stride = reference_y_stride(dst_dimensions);
    const int      u_stride = reference_uv_stride(dst_dimensions);
    const int      v_stride = reference_uv_stride(dst_dimensions);

    uint8_t *const y_plane  = dst;
    uint8_t *const u_plane  = y_plane + reference_y_size(dst_dimensions, y_stride);
    uint8_t *const v_plane  = u_plane + reference_uv_size(dst_dimensions, u_stride);

    for (int y = 0; y < dst_dimensions.height_px; y += 2) {
        for (int x = 0; x < dst_dimensions.width_px; x += 2) {
            const uint8_t *const p00 = dst_pre_transformed + ((size_t)(y + 0) * dst_pre_transformed_stride) + ((x + 0) * RGBA32_PIXEL_STRIDE);
            const uint8_t *const p01 = dst_pre_transformed + ((size_t)(y + 0) * dst_pre_transformed_stride) + ((x + 1) * RGBA32_PIXEL_STRIDE);
            const uint8_t *const p10 = dst_pre_transformed + ((size_t)(y + 1) * dst_pre_transformed_stride) + ((x + 0) * RGBA32_PIXEL_STRIDE);
            const uint8_t *const p11 = dst_pre_transformed + ((size_t)(y + 1) * dst_pre_transformed_stride) + ((x + 1) * RGBA32_PIXEL_STRIDE);

            y_plane[(size_t)(y + 0) * y_stride + (x + 0)] = reference_rgba32_to_yuv_y(p00[0], p00[1], p00[2]);
            y_plane[(size_t)(y + 0) * y_stride + (x + 1)] = reference_rgba32_to_yuv_y(p01[0], p01[1], p01[2]);
            y_plane[(size_t)(y + 1) * y_stride + (x + 0)] = reference_rgba32_to_yuv_y(p10[0], p10[1], p10[2]);
            y_plane[(size_t)(y + 1) * y_stride + (x + 1)] = reference_rgba32_to_yuv_y(p11[0], p11[1], p11[2]);

            const int sum_r = p00[0] + p01[0] + p10[0] + p11[0];
            const int sum_g = p00[1] + p01[1] + p10[1] + p11[1];
            const int sum_b = p00[2] + p01[2] + p10[2] + p11[2];

            u_plane[(size_t)(y / 2) * u_stride + (x / 2)] = reference_rgba32_to_yuv_u(sum_r, sum_g, sum_b);
            v_plane[(size_t)(y / 2) * v_stride + (x / 2)] = reference_rgba32_to_yuv_v(sum_r, sum_g, sum_b);
        }
    }

    dst_planes->y        = y_plane;
    dst_planes->y_stride = y_stride;
    dst_planes->u        = u_plane;
    dst_planes->u_stride = u_stride;
    dst_planes->v        = v_plane;
    dst_planes->v_stride = v_stride;

    free(dst_pre_transformed);
    return true;
}


#endif
