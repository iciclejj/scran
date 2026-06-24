#include <stddef.h>

#include "scranrot.h"
#include "../util.h"
#include "../generic.h"


enum {
    KERNEL_TILE_WIDTH_PX  = 2,
    KERNEL_TILE_HEIGHT_PX = 2,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};


SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
static inline void
extract_rgb(
    uint32_t pixel,
    uint32_t rgba_shuffle_mask,
    int *r, int *g, int *b
) {
    *r = (pixel >> (((rgba_shuffle_mask      ) & 0xFF) * 8)) & 0xFF;
    *g = (pixel >> (((rgba_shuffle_mask >>  8) & 0xFF) * 8)) & 0xFF;
    *b = (pixel >> (((rgba_shuffle_mask >> 16) & 0xFF) * 8)) & 0xFF;
}

SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
static inline uint8_t
compute_yuv_y(int r, int g, int b) {
    return (77 * r + 150 * g + 29 * b) >> 8;
}

// 131584 == 32896 * 4. Offset and division by 4 are merged into a single >>10.
SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
static inline uint8_t
compute_yuv_u(int sum_r, int sum_g, int sum_b) {
    return (-43 * sum_r -  84 * sum_g + 127 * sum_b + 131584) >> 10;
}

SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
static inline uint8_t
compute_yuv_v(int sum_r, int sum_g, int sum_b) {
    return (127 * sum_r - 106 * sum_g -  21 * sum_b + 131584) >> 10;
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer_to_yuv__fallback__rotate_270(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const void *_rgba32_shuffle_mask
) {
    const uint32_t rgba32_shuffle_mask = *(uint32_t *)_rgba32_shuffle_mask;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "270 kernel assumes 2x2 RGBA32 tile");

    for (int y = 0; y < src_height_px; y += 2) {
        for (int x = 0; x < src_width_px; x += 2) {

            const uint32_t p00 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            dst_y[(src_width_px-1-x) * dst_y_stride + (y  )] = compute_yuv_y(r00, g00, b00);
            dst_y[(src_width_px-2-x) * dst_y_stride + (y  )] = compute_yuv_y(r10, g10, b10);
            dst_y[(src_width_px-1-x) * dst_y_stride + (y+1)] = compute_yuv_y(r01, g01, b01);
            dst_y[(src_width_px-2-x) * dst_y_stride + (y+1)] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            dst_u[((src_width_px-2-x)/2) * dst_u_stride + (y/2)] = compute_yuv_u(sum_r, sum_g, sum_b);
            dst_v[((src_width_px-2-x)/2) * dst_v_stride + (y/2)] = compute_yuv_v(sum_r, sum_g, sum_b);
        }
    }
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer_to_yuv__fallback__rotate_180(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const void *_rgba32_shuffle_mask
) {
    const uint32_t rgba32_shuffle_mask = *(uint32_t *)_rgba32_shuffle_mask;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "180 kernel assumes 2x2 RGBA32 tile");

    for (int y = 0; y < src_height_px; y += 2) {
        for (int x = 0; x < src_width_px; x += 2) {

            const uint32_t p00 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            dst_y[(src_height_px-1-y) * dst_y_stride + (src_width_px-1-x)] = compute_yuv_y(r00, g00, b00);
            dst_y[(src_height_px-1-y) * dst_y_stride + (src_width_px-2-x)] = compute_yuv_y(r10, g10, b10);
            dst_y[(src_height_px-2-y) * dst_y_stride + (src_width_px-1-x)] = compute_yuv_y(r01, g01, b01);
            dst_y[(src_height_px-2-y) * dst_y_stride + (src_width_px-2-x)] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            dst_u[((src_height_px-2-y)/2) * dst_u_stride + ((src_width_px-2-x)/2)] = compute_yuv_u(sum_r, sum_g, sum_b);
            dst_v[((src_height_px-2-y)/2) * dst_v_stride + ((src_width_px-2-x)/2)] = compute_yuv_v(sum_r, sum_g, sum_b);
        }
    }
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer_to_yuv__fallback__rotate_90(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const void *_rgba32_shuffle_mask
) {
    const uint32_t rgba32_shuffle_mask = *(uint32_t *)_rgba32_shuffle_mask;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "90 kernel assumes 2x2 RGBA32 tile");

    for (int y = 0; y < src_height_px; y += 2) {
        for (int x = 0; x < src_width_px; x += 2) {

            const uint32_t p00 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            dst_y[(x  ) * dst_y_stride + (src_height_px-1-y)] = compute_yuv_y(r00, g00, b00);
            dst_y[(x+1) * dst_y_stride + (src_height_px-1-y)] = compute_yuv_y(r10, g10, b10);
            dst_y[(x  ) * dst_y_stride + (src_height_px-2-y)] = compute_yuv_y(r01, g01, b01);
            dst_y[(x+1) * dst_y_stride + (src_height_px-2-y)] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            dst_u[(x/2) * dst_u_stride + ((src_height_px-2-y)/2)] = compute_yuv_u(sum_r, sum_g, sum_b);
            dst_v[(x/2) * dst_v_stride + ((src_height_px-2-y)/2)] = compute_yuv_v(sum_r, sum_g, sum_b);
        }
    }
}


SCRANROT_TARGET_FALLBACK
static void
transform_framebuffer_to_yuv__fallback__rotate_0(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const void *_rgba32_shuffle_mask
) {
    const uint32_t rgba32_shuffle_mask = *(uint32_t *)_rgba32_shuffle_mask;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 2 && KERNEL_TILE_HEIGHT_PX == 2, "0 kernel assumes 2x2 RGBA32 tile");

    for (int y = 0; y < src_height_px; y += 2) {
        for (int x = 0; x < src_width_px; x += 2) {

            const uint32_t p00 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p10 = *(const uint32_t *)((const char *)src + (y+0) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);
            const uint32_t p01 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+0) * RGBA32_PIXEL_STRIDE);
            const uint32_t p11 = *(const uint32_t *)((const char *)src + (y+1) * src_stride_bytes + (x+1) * RGBA32_PIXEL_STRIDE);

            int r00, g00, b00;
            int r10, g10, b10;
            int r01, g01, b01;
            int r11, g11, b11;
            extract_rgb(p00, rgba32_shuffle_mask, &r00, &g00, &b00);
            extract_rgb(p10, rgba32_shuffle_mask, &r10, &g10, &b10);
            extract_rgb(p01, rgba32_shuffle_mask, &r01, &g01, &b01);
            extract_rgb(p11, rgba32_shuffle_mask, &r11, &g11, &b11);

            dst_y[(y  ) * dst_y_stride + (x  )] = compute_yuv_y(r00, g00, b00);
            dst_y[(y  ) * dst_y_stride + (x+1)] = compute_yuv_y(r10, g10, b10);
            dst_y[(y+1) * dst_y_stride + (x  )] = compute_yuv_y(r01, g01, b01);
            dst_y[(y+1) * dst_y_stride + (x+1)] = compute_yuv_y(r11, g11, b11);

            const int sum_r = r00 + r10 + r01 + r11;
            const int sum_g = g00 + g10 + g01 + g11;
            const int sum_b = b00 + b10 + b01 + b11;

            dst_u[(y/2) * dst_u_stride + (x/2)] = compute_yuv_u(sum_r, sum_g, sum_b);
            dst_v[(y/2) * dst_v_stride + (x/2)] = compute_yuv_v(sum_r, sum_g, sum_b);
        }
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
        transform, &rgba_shuffle_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}
