#include "../include/scranrot.h"


// Rotates frame buffer, shuffles pixel geometry, and stores result to dst
bool
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

    return selected_function(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst,
        rgba_shuffle, transform,
        // OUT:
        dst_stride
    );
}

bool
scranrot_transform_framebuffer_to_yuv420(
    const void *src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    void *dst,
    uint32_t rgba_shuffle,
    enum scranrot_transform transform,
    // OUT:
    uint8_t **dst_y, int *dst_y_stride,
    uint8_t **dst_u, int *dst_u_stride,
    uint8_t **dst_v, int *dst_v_stride
) {
    scranrot_transform_framebuffer_to_yuv_fn *selected_function;

    // TODO: Probably make a scranrot_ctx and init function to avoid the arch
    // check and width/height checks on every call?

    if (src_width_px & 1 || src_height_px & 1) {
        // YUV420 needs height and width divisible by two
        return false;
    }

#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("ssse3")) {
        selected_function = scranrot_transform_framebuffer_to_yuv420_ssse3__unaligned;
    } else
#endif
    {
        selected_function = scranrot_transform_framebuffer_to_yuv420_fallback;
    }

    return selected_function(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst,
        rgba_shuffle, transform,
        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}

