#include <stddef.h>

#include "scranrot.h"
#include "../util.h"
#include "../generic-kernel-dispatcher.h"
#include "../implementations.h"


enum {
    KERNEL_TILE_WIDTH_PX  = 2,
    KERNEL_TILE_HEIGHT_PX = 2,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};


static inline void SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
extract_rgb(
    uint32_t pixel,
    uint32_t rgba_shuffle_mask,
    int *r, int *g, int *b
) {
    *r = (pixel >> (((rgba_shuffle_mask      ) & 0xFF) * 8)) & 0xFF;
    *g = (pixel >> (((rgba_shuffle_mask >>  8) & 0xFF) * 8)) & 0xFF;
    *b = (pixel >> (((rgba_shuffle_mask >> 16) & 0xFF) * 8)) & 0xFF;
}

static inline uint8_t SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
compute_yuv_y(int r, int g, int b) {
    // BT.709 Y' target (RGB):   0.2126, 0.7152, 0.0722
    // Fixed-point /256 (RGB):   55,     183,    19
    //
    //   This rounds 54.4 up to 55 so the coefficients sum to 257. This
    //   offsets the final >> 8 truncation's ~-0.5 mean error.
    return (55 * r + 183 * g + 19 * b) >> 8;
}

static inline uint8_t SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
saturate_uv_0_to_256_to_u8(int value) {
    // WARN: This calculation ONLY works correctly if this assert holds.
    SCRANROT_ASSERT(0 <= value && value <= 256);
    return (uint8_t)(value - (value >> 8));
}

// Fast /256 fixed-point approximation of full-range BT.709 Y'CbCr.
//
// Cb target (RGB):             -0.1146, -0.3854,  0.5000
// Fixed-point /256 (RGB):      -29,     -99,      128
//
// Cr target (RGB):              0.5000, -0.4542, -0.0458
// Fixed-point /256 (RGB):       128,    -116,     -12
//
// 131584 == 32896 * 4. Offset and division by 4 are merged into a single >>10.
static inline uint8_t SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
compute_yuv_u(int sum_r, int sum_g, int sum_b) {
    return saturate_uv_0_to_256_to_u8(
        (-29 * sum_r -  99 * sum_g + 128 * sum_b + 131584) >> 10
    );
}

static inline uint8_t SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
compute_yuv_v(int sum_r, int sum_g, int sum_b) {
    return saturate_uv_0_to_256_to_u8(
        (128 * sum_r - 116 * sum_g -  12 * sum_b + 131584) >> 10
    );
}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer_to_yuv__fallback__rotate_270(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "270 kernel assumes 2x2 RGBA32 tile");

    uint8_t const *src_tile_row = src;
    uint8_t       *dst_y_tile_col = scranrot_yuv420_y_last_row_start( dst_y, src_width_px, dst_y_stride);
    uint8_t       *dst_u_tile_col = scranrot_yuv420_uv_last_row_start(dst_u, src_width_px, dst_u_stride);
    uint8_t       *dst_v_tile_col = scranrot_yuv420_uv_last_row_start(dst_v, src_width_px, dst_v_stride);

    for (int y = 0; y < src_height_px; y += 2) {
        uint8_t const *src_row_0 = src_tile_row;
        uint8_t const *src_row_1 = src_tile_row + src_stride_bytes;
        uint8_t       *dst_y_tile = dst_y_tile_col;
        uint8_t       *dst_u_tile = dst_u_tile_col;
        uint8_t       *dst_v_tile = dst_v_tile_col;

        for (int x = 0; x < src_width_px; x += 2) {
            const uint32_t p00 = scranrot_loadu_u32(src_row_0 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = scranrot_loadu_u32(src_row_0 + 1 * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = scranrot_loadu_u32(src_row_1 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = scranrot_loadu_u32(src_row_1 + 1 * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            uint8_t *const dst_y_row_0 = dst_y_tile;
            uint8_t *const dst_y_row_1 = dst_y_tile - dst_y_stride;
            dst_y_row_0[0] = compute_yuv_y(r00, g00, b00);
            dst_y_row_1[0] = compute_yuv_y(r10, g10, b10);
            dst_y_row_0[1] = compute_yuv_y(r01, g01, b01);
            dst_y_row_1[1] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            *dst_u_tile = compute_yuv_u(sum_r, sum_g, sum_b);
            *dst_v_tile = compute_yuv_v(sum_r, sum_g, sum_b);

            src_row_0 += 2 * RGBA32_PIXEL_STRIDE;
            src_row_1 += 2 * RGBA32_PIXEL_STRIDE;
            dst_y_tile -= 2 * dst_y_stride;
            dst_u_tile -= dst_u_stride;
            dst_v_tile -= dst_v_stride;
        }

        src_tile_row += 2 * src_stride_bytes;
        dst_y_tile_col += 2;
        dst_u_tile_col += 1;
        dst_v_tile_col += 1;
    }
}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer_to_yuv__fallback__rotate_180(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "180 kernel assumes 2x2 RGBA32 tile");

    uint8_t const *src_tile_row = src;
    uint8_t       *dst_y_tile_row = scranrot_yuv420_y_last_row_end( dst_y, src_width_px, src_height_px, dst_y_stride) - 1;
    uint8_t       *dst_u_tile_row = scranrot_yuv420_uv_last_row_end(dst_u, src_width_px, src_height_px, dst_u_stride) - 1;
    uint8_t       *dst_v_tile_row = scranrot_yuv420_uv_last_row_end(dst_v, src_width_px, src_height_px, dst_v_stride) - 1;

    for (int y = 0; y < src_height_px; y += 2) {
        uint8_t const *src_row_0 = src_tile_row;
        uint8_t const *src_row_1 = src_tile_row + src_stride_bytes;
        uint8_t       *dst_y_tile = dst_y_tile_row;
        uint8_t       *dst_u_tile = dst_u_tile_row;
        uint8_t       *dst_v_tile = dst_v_tile_row;

        for (int x = 0; x < src_width_px; x += 2) {
            const uint32_t p00 = scranrot_loadu_u32(src_row_0 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = scranrot_loadu_u32(src_row_0 + 1 * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = scranrot_loadu_u32(src_row_1 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = scranrot_loadu_u32(src_row_1 + 1 * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            uint8_t *const dst_y_row_0 = dst_y_tile;
            uint8_t *const dst_y_row_1 = dst_y_tile - dst_y_stride;
            dst_y_row_0[ 0] = compute_yuv_y(r00, g00, b00);
            dst_y_row_0[-1] = compute_yuv_y(r10, g10, b10);
            dst_y_row_1[ 0] = compute_yuv_y(r01, g01, b01);
            dst_y_row_1[-1] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            *dst_u_tile = compute_yuv_u(sum_r, sum_g, sum_b);
            *dst_v_tile = compute_yuv_v(sum_r, sum_g, sum_b);

            src_row_0 += 2 * RGBA32_PIXEL_STRIDE;
            src_row_1 += 2 * RGBA32_PIXEL_STRIDE;
            dst_y_tile -= 2;
            dst_u_tile -= 1;
            dst_v_tile -= 1;
        }

        src_tile_row += 2 * src_stride_bytes;
        dst_y_tile_row -= 2 * dst_y_stride;
        dst_u_tile_row -= dst_u_stride;
        dst_v_tile_row -= dst_v_stride;
    }
}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer_to_yuv__fallback__rotate_90(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "90 kernel assumes 2x2 RGBA32 tile");

    const int dst_width_px = src_height_px;

    uint8_t const *src_tile_row = src;
    uint8_t       *dst_y_tile_col = scranrot_yuv420_y_row_end( dst_y, dst_width_px) - 1;
    uint8_t       *dst_u_tile_col = scranrot_yuv420_uv_row_end(dst_u, dst_width_px) - 1;
    uint8_t       *dst_v_tile_col = scranrot_yuv420_uv_row_end(dst_v, dst_width_px) - 1;

    for (int y = 0; y < src_height_px; y += 2) {
        uint8_t const *src_row_0 = src_tile_row;
        uint8_t const *src_row_1 = src_tile_row + src_stride_bytes;
        uint8_t       *dst_y_tile = dst_y_tile_col;
        uint8_t       *dst_u_tile = dst_u_tile_col;
        uint8_t       *dst_v_tile = dst_v_tile_col;

        for (int x = 0; x < src_width_px; x += 2) {
            const uint32_t p00 = scranrot_loadu_u32(src_row_0 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = scranrot_loadu_u32(src_row_0 + 1 * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = scranrot_loadu_u32(src_row_1 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = scranrot_loadu_u32(src_row_1 + 1 * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            uint8_t *const dst_y_row_0 = dst_y_tile;
            uint8_t *const dst_y_row_1 = dst_y_tile + dst_y_stride;
            dst_y_row_0[ 0] = compute_yuv_y(r00, g00, b00);
            dst_y_row_1[ 0] = compute_yuv_y(r10, g10, b10);
            dst_y_row_0[-1] = compute_yuv_y(r01, g01, b01);
            dst_y_row_1[-1] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            *dst_u_tile = compute_yuv_u(sum_r, sum_g, sum_b);
            *dst_v_tile = compute_yuv_v(sum_r, sum_g, sum_b);

            src_row_0 += 2 * RGBA32_PIXEL_STRIDE;
            src_row_1 += 2 * RGBA32_PIXEL_STRIDE;
            dst_y_tile += 2 * dst_y_stride;
            dst_u_tile += dst_u_stride;
            dst_v_tile += dst_v_stride;
        }

        src_tile_row += 2 * src_stride_bytes;
        dst_y_tile_col -= 2;
        dst_u_tile_col -= 1;
        dst_v_tile_col -= 1;
    }
}


static void SCRANROT_TARGET_FALLBACK
transform_framebuffer_to_yuv__fallback__rotate_0(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "0 kernel assumes 2x2 RGBA32 tile");

    uint8_t const *src_tile_row = src;
    uint8_t       *dst_y_tile_row = dst_y;
    uint8_t       *dst_u_tile_row = dst_u;
    uint8_t       *dst_v_tile_row = dst_v;

    for (int y = 0; y < src_height_px; y += 2) {
        uint8_t const *src_row_0 = src_tile_row;
        uint8_t const *src_row_1 = src_tile_row + src_stride_bytes;
        uint8_t       *dst_y_tile = dst_y_tile_row;
        uint8_t       *dst_u_tile = dst_u_tile_row;
        uint8_t       *dst_v_tile = dst_v_tile_row;

        for (int x = 0; x < src_width_px; x += 2) {
            const uint32_t p00 = scranrot_loadu_u32(src_row_0 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = scranrot_loadu_u32(src_row_0 + 1 * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = scranrot_loadu_u32(src_row_1 + 0 * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = scranrot_loadu_u32(src_row_1 + 1 * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            uint8_t *const dst_y_row_0 = dst_y_tile;
            uint8_t *const dst_y_row_1 = dst_y_tile + dst_y_stride;
            dst_y_row_0[0] = compute_yuv_y(r00, g00, b00);
            dst_y_row_0[1] = compute_yuv_y(r10, g10, b10);
            dst_y_row_1[0] = compute_yuv_y(r01, g01, b01);
            dst_y_row_1[1] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            *dst_u_tile = compute_yuv_u(sum_r, sum_g, sum_b);
            *dst_v_tile = compute_yuv_v(sum_r, sum_g, sum_b);

            src_row_0 += 2 * RGBA32_PIXEL_STRIDE;
            src_row_1 += 2 * RGBA32_PIXEL_STRIDE;
            dst_y_tile += 2;
            dst_u_tile += 1;
            dst_v_tile += 1;
        }

        src_tile_row += 2 * src_stride_bytes;
        dst_y_tile_row += 2 * dst_y_stride;
        dst_u_tile_row += dst_u_stride;
        dst_v_tile_row += dst_v_stride;
    }
}


bool
scranrot_transform_framebuffer_to_yuv420_fallback(
    const uint8_t *restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *restrict dst,
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    // OUT:
    uint8_t **dst_y, int *dst_y_stride,
    uint8_t **dst_u, int *dst_u_stride,
    uint8_t **dst_v, int *dst_v_stride
) {
    _Static_assert(MIN_TILE_WIDTH_PX == 2 && MIN_TILE_HEIGHT_PX == 2,
                   "2x2 is the minimum possible YUV420 size. Our fallback kernels should support this.");
    if (SCRANROT_UNLIKELY(src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX)) {
        return false;
    }

    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);

    scranrot_transform_framebuffer_to_yuv_impl_fn transform_fn = NULL;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_to_yuv__fallback__rotate_270; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv__fallback__rotate_180; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv__fallback__rotate_90;  break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv__fallback__rotate_0;   break;
    default:
        // XXX TODO: Implement flipped
        return false;
    }

    SCRANROT_ASSERT(transform_fn != NULL);
    return transform_framebuffer_to_yuv420__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst,

        transform_fn,
        transform, rgba_shuffle_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}
