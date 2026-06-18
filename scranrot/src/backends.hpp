#ifndef SCRANROT_BACKENDS_HPP
#define SCRANROT_BACKENDS_HPP


#include "../include/scranrot.h"
#include "./types.hpp"


#define SCRANROT_ALWAYS_INLINE \
    __attribute__((always_inline))
#define SCRANROT_TARGET_SSSE3 \
    __attribute__((target("ssse3")))
#define SCRANROT_TARGET_FALLBACK


namespace scranrot::internal {

    typedef bool transform_framebuffer_to_yuv_fn(
        const u8 *__restrict src,
        int src_width_px,
        int src_height_px,
        int src_stride_bytes,
        u8 *__restrict dst,
        // Reorders dst's pixel byte-order relative to src.
        //   8-bit-valued mask representing new order
        //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
        u32 rgba_shuffle_mask,
        enum scranrot_transform transform,
        // OUT:
        u8 **dst_y, int *dst_y_stride,
        u8 **dst_u, int *dst_u_stride,
        u8 **dst_v, int *dst_v_stride
    );

    typedef bool transform_framebuffer_fn(
        const u8 *__restrict src,
        int src_width_px,
        int src_height_px,
        int src_stride_bytes,
        u8 *__restrict dst,
        // Reorders dst's pixel byte-order relative to src.
        //   8-bit-valued mask representing new order
        //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
        u32 rgba_shuffle,
        enum scranrot_transform transform,
        // OUT:
        uintptr_t *dst_stride
    );

    transform_framebuffer_fn transform_framebuffer_ssse3__unaligned;
    transform_framebuffer_fn transform_framebuffer_fallback;
    transform_framebuffer_to_yuv_fn transform_framebuffer_to_yuv420_ssse3__unaligned;
    transform_framebuffer_to_yuv_fn transform_framebuffer_to_yuv420_fallback;

}


#endif
