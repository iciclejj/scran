#ifndef SCRANROT_UTIL_H
#define SCRANROT_UTIL_H


#include "scranrot.h"


#ifndef SCRANROT_ASSERT
#include <assert.h>
#define SCRANROT_ASSERT assert
#endif

#define SCRANROT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SCRANROT_UNLIKELY(x) __builtin_expect(!!(x), 0)


// load unaligned
static inline uint32_t
scranrot_loadu_u32(const void *src)
{
    uint32_t val;
    __builtin_memcpy(&val, src, sizeof(val));
    return val;
}

// store unaligned
static inline void
scranrot_storeu_u32(void *dst, uint32_t val)
{
    __builtin_memcpy(dst, &val, sizeof(val));
}


static inline int
scranrot_get_transformed_height(int src_width, int src_height, enum scranrot_transform transform)
{
    return transform == SCRANROT_TRANSFORM_90
        || transform == SCRANROT_TRANSFORM_FLIPPED_90
        || transform == SCRANROT_TRANSFORM_270
        || transform == SCRANROT_TRANSFORM_FLIPPED_270
         ? src_width
         : src_height;
}

static inline int
scranrot_get_transformed_width(int src_width, int src_height, enum scranrot_transform transform)
{
    return transform == SCRANROT_TRANSFORM_90
        || transform == SCRANROT_TRANSFORM_FLIPPED_90
        || transform == SCRANROT_TRANSFORM_270
        || transform == SCRANROT_TRANSFORM_FLIPPED_270
         ? src_height
         : src_width;
}

static inline int
scranrot_rgba32_px_to_bytes(int px) {
    return px * RGBA32_PIXEL_STRIDE;
}

static inline uint8_t *
scranrot_rgba32_row_end(uint8_t *row_start, int width_px) {
    return row_start + scranrot_rgba32_px_to_bytes(width_px);
}

static inline uint8_t *
scranrot_rgba32_last_row_start(uint8_t *first_row_start, int height_px, int stride_bytes) {
    return first_row_start + ((height_px - 1) * stride_bytes);
}

static inline uint8_t *
scranrot_rgba32_last_row_end(uint8_t *first_row_start, int width_px, int height_px, int stride_bytes) {
    return scranrot_rgba32_row_end(
               scranrot_rgba32_last_row_start(first_row_start, height_px, stride_bytes),
               width_px
           );
}

static inline uint8_t *
scranrot_yuv420_y_row_end(uint8_t *row_start, int y_width) {
    return row_start + (y_width);
}

static inline uint8_t *
scranrot_yuv420_y_last_row_start(uint8_t *first_row_start, int y_height, int y_stride_bytes) {
    return first_row_start + ((y_height - 1) * y_stride_bytes);
}

static inline uint8_t *
scranrot_yuv420_y_last_row_end(uint8_t *first_row_start, int y_width, int y_height, int y_stride_bytes) {
    return scranrot_yuv420_y_row_end(
               scranrot_yuv420_y_last_row_start(first_row_start, y_height, y_stride_bytes),
               y_width
           );

}

static inline uint8_t *
scranrot_yuv420_uv_row_end(uint8_t *row_start, int y_width) {
    int uv_width = y_width / 2;
    return row_start + (uv_width);
}

static inline uint8_t *
scranrot_yuv420_uv_last_row_start(uint8_t *first_row_start, int y_height, int uv_stride_bytes) {
    int uv_height = y_height / 2;
    return first_row_start + ((uv_height - 1) * uv_stride_bytes);
}

static inline uint8_t *
scranrot_yuv420_uv_last_row_end(uint8_t *first_row_start, int y_width, int y_height, int uv_stride_bytes) {
    return scranrot_yuv420_uv_row_end(
               scranrot_yuv420_uv_last_row_start(first_row_start, y_height, uv_stride_bytes),
               y_width
           );

}


#endif
