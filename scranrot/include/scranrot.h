#ifndef SCRANROT_SCRANROT_H
#define SCRANROT_SCRANROT_H


/* ~ because scrot was taken ~ */


/* Copyright (c) 2026 iciclejj. MIT License, same as scran. */



#include <stdint.h>
#include <stdbool.h>


#define SCRANROT_ALWAYS_INLINE \
    __attribute__((always_inline))
#define SCRANROT_TARGET_SSSE3 \
    __attribute__((optimize("O3"))) \
    __attribute__((target("ssse3")))
// TODO: Make TARGET_FALLBACK more robust against vectorization etc?
#define SCRANROT_TARGET_FALLBACK \
    __attribute__((optimize("O3")))

#define RGBA32_PIXEL_STRIDE 4


// This is enum is equivalent to Wayland's `enum wl_output_transform`.
// All rotations are counter-clockwise
enum scranrot_transform {
	SCRANROT_TRANSFORM_NORMAL      = 0,
	SCRANROT_TRANSFORM_90          = 1,
	SCRANROT_TRANSFORM_180         = 2,
	SCRANROT_TRANSFORM_270         = 3,
	SCRANROT_TRANSFORM_FLIPPED     = 4,
	SCRANROT_TRANSFORM_FLIPPED_90  = 5,
	SCRANROT_TRANSFORM_FLIPPED_180 = 6,
	SCRANROT_TRANSFORM_FLIPPED_270 = 7,
};

typedef bool scranrot_transform_framebuffer_to_yuv_fn(
    const uint8_t *restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *restrict dst,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    // OUT:
    uint8_t **dst_y, int *dst_y_stride,
    uint8_t **dst_u, int *dst_u_stride,
    uint8_t **dst_v, int *dst_v_stride
);

typedef bool scranrot_transform_framebuffer_fn(
    const uint8_t *restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *restrict dst,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    uintptr_t *dst_stride
);


// scranrot_init() must be called before using this function.
bool
scranrot_transform_framebuffer(
    const uint8_t *restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *restrict dst,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    uintptr_t *dst_stride
);

// scranrot_init() must be called before using this function.
bool
scranrot_transform_framebuffer_to_yuv420(
    const uint8_t *restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *restrict dst,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    uint8_t **dst_y, int *dst_y_stride,
    uint8_t **dst_u, int *dst_u_stride,
    uint8_t **dst_v, int *dst_v_stride
);

// Selects appropriate implementation for the running CPU.
// Must be called once before any scranrot_transform_* function can be used.
void
scranrot_init(void);

scranrot_transform_framebuffer_fn scranrot_transform_framebuffer_ssse3;
scranrot_transform_framebuffer_fn scranrot_transform_framebuffer_fallback;
scranrot_transform_framebuffer_to_yuv_fn scranrot_transform_framebuffer_to_yuv420_ssse3;
scranrot_transform_framebuffer_to_yuv_fn scranrot_transform_framebuffer_to_yuv420_fallback;


#endif
