#include "../include/scranrot.h"


// XXX TODO:
//     Revise this description after refactor into standalone lib and
//     finished yuv conversion code
//
// Rotates frame buffer, shuffles pixel geometry, and stores result to dst
//
// NOTE: If either of [src, width, height] are not divisible by the
// SIMD-required alignment, then this function will assume that we have enough
// available padding in *both directions* for all of them.
//     `dst` must be already aligned
void
scranrot_transform_framebuffer(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {
    scranrot_transform_framebuffer_fn *selected_function;


#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("ssse3")) {
        // selected_function = _transform_framebuffer_ssse3;
        selected_function = scranrot_transform_framebuffer_ssse3__unaligned;
    } else
#endif
    {
        selected_function = scranrot_transform_framebuffer_fallback;
    }

    selected_function(
        src,
        dst,
        src_width_px,
        src_height_px,
        src_stride_bytes,
        // Reorder src pixel byte-order before moving to dst
        // 8-bit-valued mask representing new order
        //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
        rgba_shuffle,
        transform,
        dst_with_offset,
        dst_stride
    );
}

