#ifndef SCRAN_SIMD_H
#define SCRAN_SIMD_H

#include <stdint.h>

#include <wayland-client.h>

void
transform_framebuffer(
    const void *src, void *dst,
    int src_width_px, int src_height_px, int src_stride_bytes,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum wl_output_transform transform,
    // OUT:
    void **dst_with_offset,
    uintptr_t *dst_stride
);

#endif
