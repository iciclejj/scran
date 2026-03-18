#ifndef SCRANROT_SCRANROT_H
#define SCRANROT_SCRANROT_H


/* ~ because scrot was taken ~ */


/* Copyright (c) 2026 iciclejj. MIT License, same as scran. */



#include <stdint.h>


#define SCRANROT_ALWAYS_INLINE \
    __attribute__((always_inline))
#define SCRANROT_TARGET_SSSE3 \
    __attribute__((optimize("O3"))) \
    __attribute__((target("ssse3")))
// TODO: Make TARGET_FALLBACK more robust against vectorization etc?
#define SCRANROT_TARGET_FALLBACK \
    __attribute__((optimize("O3"))) \
    __attribute__((optimize("no-tree-vectorize"))) \
    __attribute__((target("no-sse")))

#define RGBA32_PIXEL_STRIDE 4
#define SCRANROT_SSE_ROW_STRIDE 4 // Number of rows we will process at a time
#define SCRANROT_FALLBACK_STRIDE_PX 4


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

typedef void scranrot_transform_framebuffer_fn(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    void **dst_with_offset,
    uintptr_t *dst_stride
);


// Main dispatcher function. Selects appropriate simd instruction set
// (or fallback) at runtime, based on cpuid.
void
scranrot_transform_framebuffer(
    const void *src, void *dst,
    int src_width_px, int src_height_px, int src_stride_bytes,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    void **dst_with_offset,
    uintptr_t *dst_stride
);

scranrot_transform_framebuffer_fn scranrot_transform_framebuffer_ssse3__unaligned;
scranrot_transform_framebuffer_fn scranrot_transform_framebuffer_fallback;


#endif
