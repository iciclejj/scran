#include "../include/scranrot.h"


// Rotates frame buffer, shuffles pixel geometry, and stores result to dst
void
scranrot_transform_framebuffer(
    const void *src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    void *dst,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    uintptr_t *dst_stride
) {
    scranrot_transform_framebuffer_fn *selected_function;


#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("ssse3")) {
        selected_function = scranrot_transform_framebuffer_ssse3__unaligned;
    } else
#endif
    {
        selected_function = scranrot_transform_framebuffer_fallback;
    }

    selected_function(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst,
        rgba_shuffle, transform,
        // OUT:
        dst_stride
    );
}

