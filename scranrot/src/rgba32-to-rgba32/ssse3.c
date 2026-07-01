#if defined(__x86_64__) || defined(__i386__)


#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSSE3
#include <stddef.h>

#include "scranrot.h"
#include "../util.h"
#include "../util-sse2.h"
#include "../generic-kernel-dispatcher.h"
#include "../implementations.h"


enum {
    RGBA32_PIXELS_PER_XMM = 4,

    KERNEL_TILE_WIDTH_PX  = RGBA32_PIXELS_PER_XMM,
    KERNEL_TILE_HEIGHT_PX = 4,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};
_Static_assert(RGBA32_PIXELS_PER_XMM * RGBA32_PIXEL_STRIDE == sizeof(__m128i), "This file assumes an XMM register holds 4 RGBA32 pixels.");


// TODO: Consider adding aligned and/or streamed versions of the sse functions
//           Initial testing did not show a significant difference for simple
//           image capture, on a 5600h CPU.


// TODO: Use KERNEL_TILE_HEIGHT_PX for an unrolled loop in these for easier tweaking
static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
load_tile_rows_unaligned(
    __m128i rows[static KERNEL_TILE_HEIGHT_PX],
    const uint8_t *row_addrs[static KERNEL_TILE_HEIGHT_PX]
) {
    rows[0] = scranrot_sse2_loadu_m128i(row_addrs[0]);
    rows[1] = scranrot_sse2_loadu_m128i(row_addrs[1]);
    rows[2] = scranrot_sse2_loadu_m128i(row_addrs[2]);
    rows[3] = scranrot_sse2_loadu_m128i(row_addrs[3]);
}

static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
store_tile_rows_unaligned(
    __m128i rows[static KERNEL_TILE_HEIGHT_PX],
    uint8_t *row_addrs[static KERNEL_TILE_HEIGHT_PX]
) {
    scranrot_sse2_storeu_m128i(row_addrs[0], rows[0]);
    scranrot_sse2_storeu_m128i(row_addrs[1], rows[1]);
    scranrot_sse2_storeu_m128i(row_addrs[2], rows[2]);
    scranrot_sse2_storeu_m128i(row_addrs[3], rows[3]);
}

static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
get_src_tile_row_addresses(
    const uint8_t *row_addrs[static KERNEL_TILE_HEIGHT_PX],
    const uint8_t *row_addr_0,
    int src_stride_bytes
) {
    row_addrs[0] = row_addr_0;
    row_addrs[1] = row_addr_0 + 1 * src_stride_bytes;
    row_addrs[2] = row_addr_0 + 2 * src_stride_bytes;
    row_addrs[3] = row_addr_0 + 3 * src_stride_bytes;
}

static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
get_dst_tile_row_addresses(
    uint8_t *row_addrs[static KERNEL_TILE_HEIGHT_PX],
    uint8_t *row_addr_0,
    int dst_stride_bytes
) {
    row_addrs[0] = row_addr_0;
    row_addrs[1] = row_addr_0 + 1 * dst_stride_bytes;
    row_addrs[2] = row_addr_0 + 2 * dst_stride_bytes;
    row_addrs[3] = row_addr_0 + 3 * dst_stride_bytes;
}

static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
convert_tile_pixel_format(
    __m128i rows[static KERNEL_TILE_HEIGHT_PX],
    __m128i shuffle_mask
) {
    rows[0] = _mm_shuffle_epi8(rows[0], shuffle_mask);
    rows[1] = _mm_shuffle_epi8(rows[1], shuffle_mask);
    rows[2] = _mm_shuffle_epi8(rows[2], shuffle_mask);
    rows[3] = _mm_shuffle_epi8(rows[3], shuffle_mask);
}

static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
rotate_tile_270(
    __m128i src_rows[static KERNEL_TILE_HEIGHT_PX],
    __m128i dst_rows[static KERNEL_TILE_HEIGHT_PX]
) {
    const __m128i dst_row_3lo_2lo = _mm_unpacklo_epi32(src_rows[0], src_rows[1]);
    const __m128i dst_row_3hi_2hi = _mm_unpacklo_epi32(src_rows[2], src_rows[3]);
    const __m128i dst_row_1lo_0lo = _mm_unpackhi_epi32(src_rows[0], src_rows[1]);
    const __m128i dst_row_1hi_0hi = _mm_unpackhi_epi32(src_rows[2], src_rows[3]);
    dst_rows[3] = _mm_unpacklo_epi64(dst_row_3lo_2lo, dst_row_3hi_2hi);
    dst_rows[2] = _mm_unpackhi_epi64(dst_row_3lo_2lo, dst_row_3hi_2hi);
    dst_rows[1] = _mm_unpacklo_epi64(dst_row_1lo_0lo, dst_row_1hi_0hi);
    dst_rows[0] = _mm_unpackhi_epi64(dst_row_1lo_0lo, dst_row_1hi_0hi);
}

static inline void SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
rotate_tile_90(
    __m128i src_rows[static KERNEL_TILE_HEIGHT_PX],
    __m128i dst_rows[static KERNEL_TILE_HEIGHT_PX]
) {
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


static void SCRANROT_TARGET_SSSE3
transform_framebuffer__ssse3_unaligned__rotate_270(
    const uint8_t *restrict src,
    const int src_width_px, // Width of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const uint32_t rgba32_shuffle_mask
) {
    __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba32_shuffle_mask);

    _Static_assert(KERNEL_TILE_WIDTH_PX == 4 && KERNEL_TILE_HEIGHT_PX == 4, "270 kernel assumes 4x4 RGBA32 tiles.");

    __m128i src_block_rows[KERNEL_TILE_HEIGHT_PX];
    const uint8_t *src_block_row_addrs[KERNEL_TILE_HEIGHT_PX];

    __m128i dst_block_rows[KERNEL_TILE_HEIGHT_PX];
    uint8_t *dst_block_row_addrs[KERNEL_TILE_HEIGHT_PX];

    const int dst_height_px = src_width_px;

    const uint8_t *src_tile_row = src;
    uint8_t       *dst_tile_col = scranrot_rgba32_last_row_start(dst, dst_height_px, dst_stride_bytes)
                                  - (KERNEL_TILE_WIDTH_PX - 1) * dst_stride_bytes;

    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += KERNEL_TILE_HEIGHT_PX) {
        const uint8_t *src_tile = src_tile_row;
        uint8_t       *dst_tile = dst_tile_col;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += KERNEL_TILE_WIDTH_PX) {
            get_src_tile_row_addresses(src_block_row_addrs, src_tile, src_stride_bytes);
            load_tile_rows_unaligned(src_block_rows, src_block_row_addrs);
            convert_tile_pixel_format (src_block_rows, rgba32_shuffle_mask_128);

            rotate_tile_270(src_block_rows, dst_block_rows);
            get_dst_tile_row_addresses(dst_block_row_addrs, dst_tile, dst_stride_bytes);
            store_tile_rows_unaligned(dst_block_rows, dst_block_row_addrs);

            src_tile += sizeof(__m128i);
            dst_tile -= dst_stride_bytes * KERNEL_TILE_WIDTH_PX;
        }

        src_tile_row += KERNEL_TILE_HEIGHT_PX * src_stride_bytes;
        dst_tile_col += KERNEL_TILE_HEIGHT_PX * RGBA32_PIXEL_STRIDE;
    }
}


static void SCRANROT_TARGET_SSSE3
transform_framebuffer__ssse3_unaligned__rotate_180(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst,
    const int dst_stride_bytes,
    const uint32_t rgba32_shuffle_mask
) {
    __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba32_shuffle_mask);

    _Static_assert(KERNEL_TILE_WIDTH_PX == 4, "180 kernel assumes 4-width RGBA32 tile");

    // NOTE: Rotation-specific:
    rgba32_shuffle_mask_128 = scranrot_sse2_rotate_180_get_modified_rgba_shuffle(rgba32_shuffle_mask_128);

    uint8_t *dst_start = scranrot_rgba32_last_row_end(dst, src_width_px, src_height_px, dst_stride_bytes)
                         - scranrot_rgba32_px_to_bytes(KERNEL_TILE_WIDTH_PX);
    assert(src_width_px % KERNEL_TILE_WIDTH_PX == 0); // Dispatcher's responsibility

    uint8_t       *dst_curr = dst_start;
    const uint8_t *src_curr = src;

    for (int src_row_px = 0; src_row_px < src_height_px; ++src_row_px) {
        uint8_t       *dst_row_base = dst_curr;
        const uint8_t *src_row_base = src_curr;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += KERNEL_TILE_WIDTH_PX) {
            __m128i src_curr_value = scranrot_sse2_loadu_m128i(src_curr);
            src_curr_value = _mm_shuffle_epi8(src_curr_value, rgba32_shuffle_mask_128);
            scranrot_sse2_storeu_m128i(dst_curr, src_curr_value);

            dst_curr -= sizeof(__m128i);
            src_curr += sizeof(__m128i);
        }

        dst_curr = dst_row_base - dst_stride_bytes;
        src_curr = src_row_base + src_stride_bytes;
    }
}


static void SCRANROT_TARGET_SSSE3
transform_framebuffer__ssse3_unaligned__rotate_90(
    const uint8_t *restrict src,
    const int src_width_px, // Width of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const uint32_t rgba32_shuffle_mask
) {
    __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba32_shuffle_mask);

    _Static_assert(KERNEL_TILE_WIDTH_PX == 4 && KERNEL_TILE_HEIGHT_PX == 4, "90 kernel assumes 4x4 RGBA32 tiles.");

    __m128i src_block_rows[KERNEL_TILE_HEIGHT_PX];
    const uint8_t *src_block_row_addrs[KERNEL_TILE_HEIGHT_PX];

    __m128i dst_block_rows[KERNEL_TILE_HEIGHT_PX];
    uint8_t *dst_block_row_addrs[KERNEL_TILE_HEIGHT_PX];

    const int dst_width_px = src_height_px;

    const uint8_t *src_tile_row = src;
    uint8_t       *dst_tile_col = scranrot_rgba32_row_end(dst, dst_width_px)
                                  - sizeof(__m128i);

    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += KERNEL_TILE_HEIGHT_PX) {
        const uint8_t *src_tile = src_tile_row;
        uint8_t       *dst_tile = dst_tile_col;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += KERNEL_TILE_WIDTH_PX) {
            get_src_tile_row_addresses(src_block_row_addrs, src_tile, src_stride_bytes);
            load_tile_rows_unaligned(src_block_rows, src_block_row_addrs);
            convert_tile_pixel_format(src_block_rows, rgba32_shuffle_mask_128);

            rotate_tile_90(src_block_rows, dst_block_rows);
            get_dst_tile_row_addresses(dst_block_row_addrs, dst_tile, dst_stride_bytes);
            store_tile_rows_unaligned(dst_block_rows, dst_block_row_addrs);

            src_tile += sizeof(__m128i);
            dst_tile += dst_stride_bytes * KERNEL_TILE_WIDTH_PX;
        }

        src_tile_row += KERNEL_TILE_HEIGHT_PX * src_stride_bytes;
        dst_tile_col -= KERNEL_TILE_HEIGHT_PX * RGBA32_PIXEL_STRIDE;
    }
}


static void SCRANROT_TARGET_SSSE3
transform_framebuffer__ssse3_unaligned__rotate_0(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict dst,
    const int dst_stride_bytes,
    const uint32_t rgba32_shuffle_mask
) {
    __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba32_shuffle_mask);

    _Static_assert(KERNEL_TILE_WIDTH_PX == 4, "0 kernel assumes 4-width RGBA32 tile");

    uint8_t       *dst_curr = dst;
    const uint8_t *src_curr = src;

    for (int src_row_px = 0; src_row_px < src_height_px; ++src_row_px) {
        uint8_t       *const dst_row_base = dst_curr;
        const uint8_t *const src_row_base = src_curr;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += KERNEL_TILE_WIDTH_PX) {

            __m128i src_curr_value = scranrot_sse2_loadu_m128i(src_curr);
            src_curr_value = _mm_shuffle_epi8(src_curr_value, rgba32_shuffle_mask_128);
            scranrot_sse2_storeu_m128i(dst_curr, src_curr_value);

            dst_curr += sizeof(__m128i);
            src_curr += sizeof(__m128i);
        }

        dst_curr = dst_row_base + dst_stride_bytes;
        src_curr = src_row_base + src_stride_bytes;
    }
}


bool
scranrot_transform_framebuffer_ssse3(
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
    uintptr_t *dst_stride
) {
    if (src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX) {
        return scranrot_transform_framebuffer_fallback(
                src, src_width_px, src_height_px, src_stride_bytes,
                dst,
                rgba_shuffle_mask, transform,
                dst_stride
        );
    }
    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);

    const int _dst_stride_px = scranrot_get_transformed_width(src_width_px, src_height_px, transform);
    // XXX: This is not needed for unaligned
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;

    scranrot_transform_framebuffer_impl_fn transform_fn = NULL;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer__ssse3_unaligned__rotate_270; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer__ssse3_unaligned__rotate_180; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer__ssse3_unaligned__rotate_90;  break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer__ssse3_unaligned__rotate_0;  break;
    default:
        // XXX TODO: Implement flipped
        return scranrot_transform_framebuffer_fallback(
                src, src_width_px, src_height_px, src_stride_bytes,
                dst,
                rgba_shuffle_mask, transform,
                dst_stride
        );
        SCRANROT_ASSERT(false);
    }

    SCRANROT_ASSERT(transform_fn != NULL);
    return transform_framebuffer__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst, dst_stride_bytes,

        transform_fn,
        transform, rgba_shuffle_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX
    );
}


#endif /* defined(__x86_64__) || defined(__i386__) */
