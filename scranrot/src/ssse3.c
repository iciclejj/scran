#if defined(__x86_64__) || defined(__i386__)


#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSE3
#include <stddef.h>

#include "../include/scranrot.h"
#include "../include/scranrot-util.h"
#include "./generic.h"


// TODO: Consider adding aligned and/or streamed versions of the sse functions
//           Initial testing did not show a significant difference for simple
//           image capture, on a 5600h CPU.



// TODO: Check stride performance on other systems (tested on 5600h)
_Static_assert(sizeof(__m128i) % RGBA32_PIXEL_STRIDE == 0, "sizeof(__m128i) is not divisible by RGBA32_PIXEL_STRIDE");
#define PIXELS_PER_M128I (sizeof(__m128i) / RGBA32_PIXEL_STRIDE)

typedef void (*_scranrot_transform_framebuffer_fn__ssse3)(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    __m128i rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
);


// TODO: Use SSE_ROW_STRIDE for an unrolled loop in these for easier tweaking
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_load_rows_unaligned(
    __m128i rows[SCRANROT_SSE_ROW_STRIDE],
    const __m128i *row_addrs[SCRANROT_SSE_ROW_STRIDE]
) {
    rows[0] = _mm_loadu_si128(row_addrs[0]);
    rows[1] = _mm_loadu_si128(row_addrs[1]);
    rows[2] = _mm_loadu_si128(row_addrs[2]);
    rows[3] = _mm_loadu_si128(row_addrs[3]);
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_store_rows_unaligned(
    __m128i rows[SCRANROT_SSE_ROW_STRIDE],
    __m128i *row_addrs[SCRANROT_SSE_ROW_STRIDE]
) {
    _mm_storeu_si128(row_addrs[0], rows[0]);
    _mm_storeu_si128(row_addrs[1], rows[1]);
    _mm_storeu_si128(row_addrs[2], rows[2]);
    _mm_storeu_si128(row_addrs[3], rows[3]);
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_get_src_row_addresses(
    const __m128i *row_addrs[SCRANROT_SSE_ROW_STRIDE],
    const char *row_addr_0,
    int src_stride_bytes
) {
    row_addrs[0] = (__m128i *)(row_addr_0);
    row_addrs[1] = (__m128i *)(row_addr_0 + 1 * src_stride_bytes);
    row_addrs[2] = (__m128i *)(row_addr_0 + 2 * src_stride_bytes);
    row_addrs[3] = (__m128i *)(row_addr_0 + 3 * src_stride_bytes);
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_get_dst_row_addresses(
    __m128i *row_addrs[SCRANROT_SSE_ROW_STRIDE],
    const char *row_addr_0,
    int dst_stride_bytes
) {
    row_addrs[0] = (__m128i *)(row_addr_0);
    row_addrs[1] = (__m128i *)(row_addr_0 + 1 * dst_stride_bytes);
    row_addrs[2] = (__m128i *)(row_addr_0 + 2 * dst_stride_bytes);
    row_addrs[3] = (__m128i *)(row_addr_0 + 3 * dst_stride_bytes);
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_convert_pixel_format(
    __m128i rows[SCRANROT_SSE_ROW_STRIDE],
    __m128i shuffle_mask
) {
    rows[0] = _mm_shuffle_epi8(rows[0], shuffle_mask);
    rows[1] = _mm_shuffle_epi8(rows[1], shuffle_mask);
    rows[2] = _mm_shuffle_epi8(rows[2], shuffle_mask);
    rows[3] = _mm_shuffle_epi8(rows[3], shuffle_mask);
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_rotate_270(
    __m128i src_rows[SCRANROT_SSE_ROW_STRIDE],
    __m128i dst_rows[SCRANROT_SSE_ROW_STRIDE]
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

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
_ssse3_rotate_180_get_modified_rgba_shuffle(
    const __m128i original_rgba_shuffle_mask
) {
    return _mm_shuffle_epi32(original_rgba_shuffle_mask, _MM_SHUFFLE(0,1,2,3));
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_ssse3_rotate_90(
    __m128i src_rows[SCRANROT_SSE_ROW_STRIDE],
    __m128i dst_rows[SCRANROT_SSE_ROW_STRIDE]
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
// - Create SCRANOT_SSE_COL_STRIDE macro.
// - Assert src and dst are already aligned
// - Handle the of the loop directionality such that we can do aligned stores
//   and reads for the main part, and only fallback to unaligned during edge/
//   corner handling?
//


SCRANROT_TARGET_SSSE3
static void
transform_framebuffer__ssse3_unaligned__rotate_270(
    const void *const restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    __m128i src_block_rows[SCRANROT_SSE_ROW_STRIDE];
    const __m128i *src_block_row_addrs[SCRANROT_SSE_ROW_STRIDE];

    __m128i dst_block_rows[SCRANROT_SSE_ROW_STRIDE];
    __m128i *dst_block_row_addrs[SCRANROT_SSE_ROW_STRIDE];


    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += SCRANROT_SSE_ROW_STRIDE) {

        const int dst_col_px = src_row_px; // NOTE: Rotation-speicific
        const int dst_col_offset_bytes = dst_col_px * RGBA32_PIXEL_STRIDE;

        // NOTE: Rotation-specific:
        // TODO: We can factor this even farther out
        char *dst_block_row_addr_0 = (char *)dst
                                     // src_width_px - PIXELS_PER_M128I because we're loading
                                     // rows 0,+1,+2,+3 on every loop (note: This also accounts
                                     // accounts for the -1 for len->index)
                                     + (src_width_px - PIXELS_PER_M128I) * dst_stride_bytes
                                     + dst_col_offset_bytes;

        const char *const src_block_row_addrs_base = (char *)src + src_row_px * src_stride_bytes;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += PIXELS_PER_M128I) {

            const char *const _src_block_row_addr_0 = src_block_row_addrs_base + src_col_px * RGBA32_PIXEL_STRIDE;

            _ssse3_get_src_row_addresses(src_block_row_addrs, _src_block_row_addr_0, src_stride_bytes);
            _ssse3_load_rows_unaligned(src_block_rows, src_block_row_addrs);
            _ssse3_convert_pixel_format (src_block_rows, rgba32_shuffle_mask_128);

            {
                _ssse3_rotate_270(src_block_rows, dst_block_rows); // NOTE: Rotation-specific

                _ssse3_get_dst_row_addresses(dst_block_row_addrs, dst_block_row_addr_0, dst_stride_bytes);
                dst_block_row_addr_0 -= dst_stride_bytes * PIXELS_PER_M128I; // NOTE: Rotation-specific
            }

            _ssse3_store_rows_unaligned(dst_block_rows, dst_block_row_addrs);
        }
    }
}


// XXX TODO: Double-check the padding and alignment for this
SCRANROT_TARGET_SSSE3
static void
transform_framebuffer__ssse3_unaligned__rotate_180(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    // NOTE: Rotation-specific:
    rgba32_shuffle_mask_128 = _ssse3_rotate_180_get_modified_rgba_shuffle(rgba32_shuffle_mask_128);

    char *const dst_last_row = (char *)dst + (src_height_px - 1) * dst_stride_bytes;

    char *dst_start = (src_width_px % PIXELS_PER_M128I) == 0
                    ? dst_last_row + RGBA32_PIXEL_STRIDE * (src_width_px - PIXELS_PER_M128I)
                    : dst_last_row + RGBA32_PIXEL_STRIDE * ((src_width_px / PIXELS_PER_M128I) * PIXELS_PER_M128I);

    __m128i *dst_curr = (__m128i *)dst_start;
    const __m128i *src_curr = src;

    for (int src_row_px = 0; src_row_px < src_height_px; ++src_row_px) {
        const __m128i *dst_row_base = dst_curr;
        const __m128i *src_row_base = src_curr;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += PIXELS_PER_M128I) {
            __m128i src_curr_value = _mm_loadu_si128(src_curr);
            src_curr_value = _mm_shuffle_epi8(src_curr_value, rgba32_shuffle_mask_128);
            _mm_storeu_si128(dst_curr, src_curr_value);

            --dst_curr;
            ++src_curr;
        }

        dst_curr = (__m128i *)((char *)dst_row_base - dst_stride_bytes);
        src_curr = (__m128i *)((char *)src_row_base + src_stride_bytes);
    }
}


SCRANROT_TARGET_SSSE3
static void
transform_framebuffer__ssse3_unaligned__rotate_90(
    const void *const restrict src,
    const int src_width_px, // Stride of the entire capture source
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    __m128i src_block_rows[SCRANROT_SSE_ROW_STRIDE];
    const __m128i *src_block_row_addrs[SCRANROT_SSE_ROW_STRIDE];

    __m128i dst_block_rows[SCRANROT_SSE_ROW_STRIDE];
    __m128i *dst_block_row_addrs[SCRANROT_SSE_ROW_STRIDE];


    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += SCRANROT_SSE_ROW_STRIDE) {
        // NOTE: Rotation-specific code:
        const int dst_col_px = (src_height_px - PIXELS_PER_M128I) - src_row_px; // -4 => len -> __m128i (4 pixels) index
        SCRANROT_ASSERT(RGBA32_PIXEL_STRIDE * (dst_col_px + PIXELS_PER_M128I) <= dst_stride_bytes); // Stay within padded bounds
        const int dst_col_offset_bytes = dst_col_px * RGBA32_PIXEL_STRIDE;
        char *dst_block_row_addr_0 = (char *)dst
                                     + dst_col_offset_bytes;

        const char *const src_block_row_addrs_base = (char *)src + src_row_px * src_stride_bytes;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += PIXELS_PER_M128I) {

            const char *const _src_block_row_addr_0 = src_block_row_addrs_base + src_col_px * RGBA32_PIXEL_STRIDE;

            _ssse3_get_src_row_addresses(src_block_row_addrs, _src_block_row_addr_0, src_stride_bytes);
            _ssse3_load_rows_unaligned(src_block_rows, src_block_row_addrs);
            _ssse3_convert_pixel_format(src_block_rows, rgba32_shuffle_mask_128);

            {
                // NOTE: Rotation-specific code
                _ssse3_rotate_90(src_block_rows, dst_block_rows);
                _ssse3_get_dst_row_addresses(dst_block_row_addrs, dst_block_row_addr_0, dst_stride_bytes);
                dst_block_row_addr_0 += dst_stride_bytes * PIXELS_PER_M128I;
            }

            _ssse3_store_rows_unaligned(dst_block_rows, dst_block_row_addrs);
        }
    }
}


SCRANROT_TARGET_SSSE3
static void
transform_framebuffer__ssse3_unaligned__rotate_0(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    __m128i *dst_curr = (__m128i *)dst;
    const __m128i *src_curr = src;

    for (int src_row_px = 0; src_row_px < src_height_px; ++src_row_px) {
        const __m128i *const dst_row_base = dst_curr;
        const __m128i *const src_row_base = src_curr;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += PIXELS_PER_M128I) {

            __m128i src_curr_value = _mm_loadu_si128(src_curr);
            src_curr_value = _mm_shuffle_epi8(src_curr_value, rgba32_shuffle_mask_128);
            _mm_storeu_si128(dst_curr, src_curr_value);

            ++dst_curr;
            ++src_curr;
        }

        dst_curr = (__m128i *)((char *)dst_row_base + dst_stride_bytes);
        src_curr = (__m128i *)((char *)src_row_base + src_stride_bytes);
    }
}


bool
scranrot_transform_framebuffer_ssse3__unaligned(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {

    bool dimensions_supported = src_width_px >= PIXELS_PER_M128I && src_height_px >= SCRANROT_SSE_ROW_STRIDE;
    if (!dimensions_supported) {
        return scranrot_transform_framebuffer_fallback(
                src, dst,
                src_width_px, src_height_px, src_stride_bytes,
                rgba_shuffle_mask, transform,
                dst_with_offset, dst_stride
        );
    }


    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)
    const __m128i _rgba_shuffle_mask_128_offsets = _mm_setr_epi8(0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12);
    const __m128i _rgba_shuffle_mask_128 = _mm_set1_epi32(rgba_shuffle_mask);
    const __m128i rgba_shuffle_mask_128 = _mm_add_epi8(_rgba_shuffle_mask_128_offsets, _rgba_shuffle_mask_128);

    SCRANROT_ASSERT(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = scranrot_get_transformed_width(src_width_px, src_height_px, transform);
    // XXX: This is not needed for unaligned
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;
    *dst_with_offset = dst;

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
                src, dst,
                src_width_px, src_height_px, src_stride_bytes,
                rgba_shuffle_mask, transform,
                dst_with_offset, dst_stride
        );
        assert(false);
    }

    SCRANROT_ASSERT(transform_fn != NULL);
    return transform_framebuffer__generic_dispatcher(
        src,
        src_width_px,
        src_height_px,
        src_stride_bytes,
        dst,
        dst_stride_bytes,

        transform_fn,
        transform,
        &rgba_shuffle_mask_128,
        PIXELS_PER_M128I,
        SCRANROT_SSE_ROW_STRIDE
    );
}


#endif /* defined(__x86_64__) || defined(__i386__) */
