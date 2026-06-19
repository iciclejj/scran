#if defined(__x86_64__) || defined(__i386__)


#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSE3

#include "scranrot.h"
#include "../util.hpp"
#include "../generic-dispatch.hpp"
#include "../sse2.hpp"
#include "../types.hpp"
#include "../backends.hpp"

using namespace scranrot::internal;


static constexpr auto RGBA32_PIXELS_PER_XMM = 4;
static constexpr auto TILE_WIDTH_PX = RGBA32_PIXELS_PER_XMM;
static_assert(RGBA32_PIXELS_PER_XMM * RGBA32_PIXEL_STRIDE == sizeof(__m128i), "This file assumes an XMM register holds 4 RGBA32 pixels.");


// TODO: Consider adding aligned and/or streamed versions of the sse functions
//           Initial testing did not show a significant difference for simple
//           image capture, on a 5600h CPU.


template<int TileHeightPx>
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
load_src_tile_rows_unaligned(
    __m128i (&rows_out)[TileHeightPx],
    const u8 *row_in_addr_0,
    int src_stride_bytes [[maybe_unused]]
) {
    auto fn = [&](auto i) -> void {
        const u8 *const row_in_addr = row_in_addr_0 + i * src_stride_bytes;
        rows_out[i] = load_unaligned<__m128i>(row_in_addr);
    };
    static_for<TileHeightPx>(fn);
}

template<int TileHeightPx>
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
store_dst_tile_rows_unaligned(
    const __m128i (&rows_in)[TileHeightPx],
    u8 *row_out_addr_0,
    int dst_stride_bytes [[maybe_unused]]
) {
    auto fn = [&](auto i) {
        u8 *const row_out_addr = row_out_addr_0 + i * dst_stride_bytes;
        store_unaligned(row_out_addr, rows_in[i]);
    };
    static_for<TileHeightPx>(fn);
}

template<int TileHeightPx>
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
convert_tile_pixel_format(
    __m128i (&rows)[TileHeightPx],
    __m128i shuffle_mask
) {
    // NOTE: We need explicit arch target for the lambda to play nice with static_for
    auto fn = [&](auto i) SCRANROT_TARGET_SSSE3 {
        rows[i] = _mm_shuffle_epi8(rows[i], shuffle_mask);
    };
    static_for<TileHeightPx>(fn);
}

template<int TileWidthPx, int TileHeightPx>
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
rotate_tile_270(
    __m128i (&src_rows)[TileHeightPx],
    __m128i (&dst_rows)[TileWidthPx]
) {
    static_assert(TileHeightPx == 4 && TileWidthPx == 4);
    const __m128i dst_row_3lo_2lo = _mm_unpacklo_epi32(src_rows[0], src_rows[1]);
    const __m128i dst_row_3hi_2hi = _mm_unpacklo_epi32(src_rows[2], src_rows[3]);
    const __m128i dst_row_1lo_0lo = _mm_unpackhi_epi32(src_rows[0], src_rows[1]);
    const __m128i dst_row_1hi_0hi = _mm_unpackhi_epi32(src_rows[2], src_rows[3]);
    dst_rows[3] = _mm_unpacklo_epi64(dst_row_3lo_2lo, dst_row_3hi_2hi);
    dst_rows[2] = _mm_unpackhi_epi64(dst_row_3lo_2lo, dst_row_3hi_2hi);
    dst_rows[1] = _mm_unpacklo_epi64(dst_row_1lo_0lo, dst_row_1hi_0hi);
    dst_rows[0] = _mm_unpackhi_epi64(dst_row_1lo_0lo, dst_row_1hi_0hi);
}

template<int TileWidthPx, int TileHeightPx>
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
rotate_tile_90(
    __m128i (&src_rows)[TileHeightPx],
    __m128i (&dst_rows)[TileWidthPx]
) {
    static_assert(TileHeightPx == 4 && TileWidthPx == 4);
    const __m128i dst_row_0hi_1hi = _mm_unpacklo_epi32(src_rows[1], src_rows[0]);
    const __m128i dst_row_0lo_1lo = _mm_unpacklo_epi32(src_rows[3], src_rows[2]);
    const __m128i dst_row_2hi_3hi = _mm_unpackhi_epi32(src_rows[1], src_rows[0]);
    const __m128i dst_row_2lo_3lo = _mm_unpackhi_epi32(src_rows[3], src_rows[2]);
    dst_rows[0] = _mm_unpacklo_epi64(dst_row_0lo_1lo, dst_row_0hi_1hi);
    dst_rows[1] = _mm_unpackhi_epi64(dst_row_0lo_1lo, dst_row_0hi_1hi);
    dst_rows[2] = _mm_unpacklo_epi64(dst_row_2lo_3lo, dst_row_2hi_3hi);
    dst_rows[3] = _mm_unpackhi_epi64(dst_row_2lo_3lo, dst_row_2hi_3hi);
}


// ssse3 TODOs:
// - Prefetch? More specific tiling?
// - assert() our boundaries within the loops.
// - Assert src and dst are already aligned
// - Handle the of the loop directionality such that we can do aligned stores
//   and reads for the main part, and only fallback to unaligned during edge/
//   corner handling?
//


struct Rotate270 {
    static constexpr int TILE_HEIGHT_PX     = 4;
    static constexpr int TILE_HEIGHT_PX_DST = TILE_WIDTH_PX;

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_x_step(int dst_stride_bytes) {
        return -(TILE_WIDTH_PX * dst_stride_bytes);
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_y_step(int /*dst_stride_bytes*/) {
        return TILE_HEIGHT_PX * RGBA32_PIXEL_STRIDE;
    }

    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_walk_start_address(u8 *dst, int dst_stride_bytes, Point src_max) {
        const int src_width = src_max.x + 1;
        // Subtracting so that the first write isn't writing out of bounds
        return dst + ((src_width - TILE_WIDTH_PX) * dst_stride_bytes);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void modify_shuffle_mask(__m128i &/*mask*/) { }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_with_copy(auto &dst_tile_rows, auto &src_tile_rows) {
        rotate_tile_270(src_tile_rows, dst_tile_rows);
    }
};


struct Rotate90 {
    static constexpr int TILE_HEIGHT_PX     = 4;
    static constexpr int TILE_HEIGHT_PX_DST = TILE_WIDTH_PX;

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_x_step(int dst_stride_bytes) {
        return TILE_WIDTH_PX * dst_stride_bytes;
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_y_step(int /*dst_stride_bytes*/) {
        return -(TILE_HEIGHT_PX * RGBA32_PIXEL_STRIDE);
    }

    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_walk_start_address(u8 *dst, int /*dst_stride_bytes*/, Point src_max) {
        const int src_height = src_max.y + 1;
        // Subtracting so that the first write isn't writing out of bounds
        return dst + ((src_height - TILE_HEIGHT_PX) * RGBA32_PIXEL_STRIDE);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void modify_shuffle_mask(__m128i &/*mask*/) { }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_with_copy(auto &dst_tile_rows, auto &src_tile_rows) {
        rotate_tile_90(src_tile_rows, dst_tile_rows);
    }
};


// XXX TODO: Double-check the padding and alignment for this?
struct Rotate180 {
    static constexpr int TILE_HEIGHT_PX     = 1;
    static constexpr int TILE_HEIGHT_PX_DST = TILE_HEIGHT_PX;

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_x_step(int /*dst_stride_bytes*/) {
        return -(TILE_WIDTH_PX * RGBA32_PIXEL_STRIDE);
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_y_step(int dst_stride_bytes) {
        return -(TILE_HEIGHT_PX * dst_stride_bytes);
    }


    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_walk_start_address(u8 *dst, int dst_stride_bytes, Point src_max) {
        u8 *const dst_last_row = dst + src_max.y * dst_stride_bytes;
        const int src_width = src_max.x + 1;

        return (src_width % TILE_WIDTH_PX) == 0
               ? dst_last_row + RGBA32_PIXEL_STRIDE * (src_width - TILE_WIDTH_PX)
               : dst_last_row + RGBA32_PIXEL_STRIDE * ((src_width / TILE_WIDTH_PX) * TILE_WIDTH_PX);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void modify_shuffle_mask(__m128i &mask) {
        mask = scranrot_sse2_rotate_180_get_modified_rgba_shuffle(mask);
    }
};


struct Rotate0 {
    static constexpr int TILE_HEIGHT_PX     = 1;
    static constexpr int TILE_HEIGHT_PX_DST = TILE_HEIGHT_PX;

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_x_step(int /*dst_stride_bytes*/) {
        return TILE_WIDTH_PX * RGBA32_PIXEL_STRIDE;
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline int dst_step_for_src_y_step(int dst_stride_bytes) {
        return dst_stride_bytes;
    }

    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_walk_start_address(u8 *dst, int /*dst_stride_bytes*/, Point /*src_max*/) {
        return dst;
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void modify_shuffle_mask(__m128i &/*mask*/) { }
};


template<typename Rotation>
SCRANROT_TARGET_SSSE3
static void
transform_framebuffer_ssse3_impl(
    const u8 *__restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    u8 *__restrict dst,
    const int dst_stride_bytes,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i rgba32_shuffle_mask_128 = load_unaligned<__m128i>(_rgba32_shuffle_mask_128);

    Rotation::modify_shuffle_mask(rgba32_shuffle_mask_128);

    static_assert(Rotation::TILE_HEIGHT_PX_DST == Rotation::TILE_HEIGHT_PX ||
                  Rotation::TILE_HEIGHT_PX_DST == TILE_WIDTH_PX);

    __m128i src_tile_rows[Rotation::TILE_HEIGHT_PX];
    __m128i dst_tile_rows[Rotation::TILE_HEIGHT_PX_DST];

    const Point src_px_max = {
        .x = src_width_px - 1,
        .y = src_height_px - 1
    };

    u8       *dst_curr = Rotation::get_dst_walk_start_address(dst, dst_stride_bytes, src_px_max);
    u8 const *src_curr = src;

    auto constexpr src_x_step =           TILE_WIDTH_PX  * RGBA32_PIXEL_STRIDE;
    auto const     src_y_step = Rotation::TILE_HEIGHT_PX * src_stride_bytes;
    auto const     dst_step_for_src_x_step = Rotation::dst_step_for_src_x_step(dst_stride_bytes);
    auto const     dst_step_for_src_y_step = Rotation::dst_step_for_src_y_step(dst_stride_bytes);

    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += Rotation::TILE_HEIGHT_PX) {
        u8       *const dst_row_base = dst_curr;
        u8 const *const src_row_base = src_curr;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += TILE_WIDTH_PX) {

            load_src_tile_rows_unaligned(src_tile_rows, src_curr, src_stride_bytes);
            convert_tile_pixel_format(src_tile_rows, rgba32_shuffle_mask_128);

            if constexpr (requires { Rotation::rotate_with_copy(dst_tile_rows, src_tile_rows); }) {
                Rotation::rotate_with_copy(dst_tile_rows, src_tile_rows);
                store_dst_tile_rows_unaligned(dst_tile_rows, dst_curr, dst_stride_bytes);
            } else {
                store_dst_tile_rows_unaligned(src_tile_rows, dst_curr, dst_stride_bytes);
            }

            dst_curr += dst_step_for_src_x_step;
            src_curr += src_x_step;
        }

        dst_curr = dst_row_base + dst_step_for_src_y_step;
        src_curr = src_row_base + src_y_step;
    }
}

template<typename Rotation>
static bool
transform_framebuffer_ssse3__unaligned__for_rotation(
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
    uintptr_t *dst_stride
) {
    if (src_width_px < TILE_WIDTH_PX || src_height_px < Rotation::TILE_HEIGHT_PX) {
        return transform_framebuffer_fallback(
                src, src_width_px, src_height_px, src_stride_bytes,
                dst,
                rgba_shuffle_mask, transform,
                dst_stride
        );
    }
    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);

    const __m128i rgba_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba_shuffle_mask);

    const int _dst_stride_px = get_transformed_width(src_width_px, src_height_px, transform);
    // XXX: This is not needed for unaligned
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;

    return transform_framebuffer__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst, dst_stride_bytes,

        transform_framebuffer_ssse3_impl<Rotation>,
        transform, &rgba_shuffle_mask_128,
        TILE_WIDTH_PX, Rotation::TILE_HEIGHT_PX
    );
}

bool
scranrot::internal::transform_framebuffer_ssse3__unaligned(
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
    uintptr_t *dst_stride
) {
    auto run = [&]<typename Rotation>() -> auto {
        return transform_framebuffer_ssse3__unaligned__for_rotation<Rotation>(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst, rgba_shuffle_mask, transform,
            dst_stride
        );
    };

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        return run.template operator()<Rotate270>(); break;
    case SCRANROT_TRANSFORM_180:
        return run.template operator()<Rotate180>(); break;
    case SCRANROT_TRANSFORM_90:
        return run.template operator()<Rotate90>();  break;
    case SCRANROT_TRANSFORM_NORMAL:
        return run.template operator()<Rotate0>();   break;
    default:
        // XXX TODO: Implement flipped
        return transform_framebuffer_fallback(
                src, src_width_px, src_height_px, src_stride_bytes,
                dst,
                rgba_shuffle_mask, transform,
                dst_stride
        );
        SCRANROT_ASSERT(false);
    }
}


#endif /* defined(__x86_64__) || defined(__i386__) */
