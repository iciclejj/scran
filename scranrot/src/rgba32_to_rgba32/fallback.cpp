#include "scranrot.h"
#include "../util.hpp"
#include "../generic-dispatch.hpp"
#include "../types.hpp"

using namespace scranrot::internal;


namespace {

    enum {
        KERNEL_TILE_WIDTH_PX  = 1, // Optimal seems to be 1 (on a 5600h, both with and without auto-vectorization)
        KERNEL_TILE_HEIGHT_PX = 4,

        MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
        MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
    };


    SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
    inline u32
    convert_pixel_format(
        u32 pixel,
        u32 rgba_shift_mask // NOTE: NOT Shuffle mask.
    ) {
        u32 converted_pixel =
                 ((pixel & 0xFF000000) >> 24) << ((rgba_shift_mask & 0xFF000000) >> 24)
               | ((pixel & 0x00FF0000) >> 16) << ((rgba_shift_mask & 0x00FF0000) >> 16)
               | ((pixel & 0x0000FF00) >> 8)  << ((rgba_shift_mask & 0x0000FF00) >> 8)
               | ((pixel & 0x000000FF))       << ((rgba_shift_mask & 0x000000FF))
        ;

        return converted_pixel;
    }


    // TODO: Make dst pointer a template parameter, so we can pre-calculate
    //       e.g. dst_last_pixel_address for 180-kernel?
    //          I.e.         dst_last_pixel_address
    //                       - x_src * PX_STRIDE
    //                       - y_src * dst_stride_bytes
    //          instead of   dst
    //                       + (y_src_max - y_src) * PX_STIDE
    //                       + (x_src_max - x_src) * dst_stride_bytes

    struct Rotate270 {
        SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
        static inline Point get_dst_point(Point src,  Point src_max) {
            return {
                .x =  src.y,
                .y = (src_max.x - src.x)
            };
        }
    };

    struct Rotate90 {
        SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
        static inline Point get_dst_point(Point src, Point src_max) {
            return {
                .x = (src_max.y - src.y),
                .y =  src.x
            };
        }
    };

    struct Rotate180 {
        SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
        static inline Point get_dst_point(Point src, Point src_max) {
            return {
                .x = (src_max.x - src.x),
                .y = (src_max.y - src.y)
            };
        }
    };

    struct Rotate0 {
        SCRANROT_TARGET_FALLBACK SCRANROT_ALWAYS_INLINE
        static inline Point get_dst_point(Point src, Point /*src_max*/) {
            return {
                .x = src.x,
                .y = src.y
            };
        }
    };

    template<typename Rotation>
    SCRANROT_TARGET_FALLBACK
    void
    transform_framebuffer_fallback_impl(
        const u8 *const __restrict src,
        const int src_width_px, // Stride of the entire capture source
        const int src_height_px,
        const int src_stride_bytes,
        u8 *const __restrict dst,
        const int dst_stride_bytes, // Stride of the final output image
        const u32 _rgba32_shuffle_mask // Mask for _mm_shuffle_epi8
    ) {
        const u32 rgba32_shift_mask = _rgba32_shuffle_mask * 8;

        static_assert(KERNEL_TILE_HEIGHT_PX == 4, "kernel assumes 4-row RGBA32 tile");

        static const int tile_height = 4;
        static const int tile_width  = KERNEL_TILE_WIDTH_PX;

        const Point src_px_max = {
            .x = src_width_px - 1,
            .y = src_height_px - 1
        };

        for (int y = 0; y < src_height_px; y += tile_height) {
            for (int x = 0; x < src_width_px; x += tile_width) {

                for (int _y = 0; _y < tile_height; ++_y) {
                    for (int _x = 0; _x < tile_width; ++_x) {

                        const Point src_px = {  (x + _x),  (y + _y)  };

                        u8 const *const _src = src
                            + src_px.y * src_stride_bytes
                            + src_px.x * RGBA32_PIXEL_STRIDE;

                        const Point dst_px = Rotation::get_dst_point(src_px, src_px_max);

                        u8 *const _dst = dst
                            + dst_px.y * dst_stride_bytes
                            + dst_px.x * RGBA32_PIXEL_STRIDE;

                        u32 val = load_unaligned<u32>(_src);
                        val = convert_pixel_format(val, rgba32_shift_mask);

                        store_unaligned(_dst, val);
                    }
                }

            }
        }
    }

}


bool
scranrot::internal::transform_framebuffer_fallback(
    const u8 *src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    u8 *dst,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    const u32 rgba32_shuffle_mask,
    enum scranrot_transform transform,
    uintptr_t *dst_stride
) {
    // XXX TODO(!!): IMPLEMENT THIS!!
    if (SCRANROT_UNLIKELY(src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX)) {
        return false;
    }

    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)

    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = get_transformed_width(src_width_px, src_height_px, transform);
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;

    transform_framebuffer_impl_fn transform_fn = nullptr;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_fallback_impl<Rotate270>; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_fallback_impl<Rotate180>; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_fallback_impl<Rotate90> ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_fallback_impl<Rotate0>  ; break;
    default:
        // XXX TODO: Implement flipped
        return false;
    }

    SCRANROT_ASSERT(transform_fn != nullptr);
    return transform_framebuffer__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst, dst_stride_bytes,
        transform_fn,
        transform, rgba32_shuffle_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX
    );
}
