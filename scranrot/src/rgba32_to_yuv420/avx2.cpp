#if defined(__x86_64__) || defined(__i386__)


#include <immintrin.h>

#include "scranrot.h"
#include "./avx2-backend.hpp"
#include "../generic-dispatch.hpp"
#include "../types.hpp"
#include "./rotations.hpp"

using namespace scranrot::internal::yuv420;


#define SCRANROT_YUV420_KERNEL_TARGET SCRANROT_TARGET_AVX2
#include "kernel.ipp"
#undef SCRANROT_YUV420_KERNEL_TARGET


bool
scranrot::internal::transform_framebuffer_to_yuv420_avx2(
    const u8 *__restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    u8 *__restrict dst,
    u32 rgba_shuffle_mask,
    enum scranrot_transform transform,
    // OUT:
    u8 **dst_y, int *dst_y_stride,
    u8 **dst_u, int *dst_u_stride,
    u8 **dst_v, int *dst_v_stride
) {
    if (src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX) {
        return transform_framebuffer_to_yuv420_fallback(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst, rgba_shuffle_mask, transform,
            dst_y, dst_y_stride,
            dst_u, dst_u_stride,
            dst_v, dst_v_stride
        );
    }

    transform_framebuffer_to_yuv_impl_fn transform_fn = nullptr;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_to_yuv420__kernel<YUV420BackendAVX2, Rotate270>; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv420__kernel<YUV420BackendAVX2, Rotate180>; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv420__kernel<YUV420BackendAVX2, Rotate90> ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv420__kernel<YUV420BackendAVX2, Rotate0>  ; break;
    default:
        // XXX TODO: Implement flipped
        return transform_framebuffer_to_yuv420_fallback(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst, rgba_shuffle_mask, transform,
            dst_y, dst_y_stride,
            dst_u, dst_u_stride,
            dst_v, dst_v_stride
        );
    }

    SCRANROT_ASSERT(transform_fn != nullptr);
    return transform_framebuffer_to_yuv420__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst,

        transform_fn,
        transform, rgba_shuffle_mask,
        TILE_WIDTH_PX, TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}


#endif
