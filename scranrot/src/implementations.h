#ifndef SCRANROT_ARCH_H
#define SCRANROT_ARCH_H


#include <stdint.h>
#include <stdbool.h>

#include "scranrot.h"


#define SCRANROT_ALWAYS_INLINE \
    __attribute__((always_inline))
#define SCRANROT_TARGET_SSE2 \
    __attribute__((target("sse2")))
#define SCRANROT_TARGET_SSSE3 \
    __attribute__((target("ssse3")))
#define SCRANROT_TARGET_AVX2 \
    __attribute__((target("avx2")))
// TODO: Make TARGET_FALLBACK more robust against vectorization etc?
#define SCRANROT_TARGET_FALLBACK \


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


scranrot_transform_framebuffer_fn scranrot_transform_framebuffer_ssse3;
scranrot_transform_framebuffer_fn scranrot_transform_framebuffer_fallback;
scranrot_transform_framebuffer_to_yuv_fn scranrot_transform_framebuffer_to_yuv420_avx2;
scranrot_transform_framebuffer_to_yuv_fn scranrot_transform_framebuffer_to_yuv420_ssse3;
scranrot_transform_framebuffer_to_yuv_fn scranrot_transform_framebuffer_to_yuv420_fallback;


#endif
