#include <stdint.h>
#include <assert.h>
#include <stdalign.h>
#include <tmmintrin.h>
#include <emmintrin.h>
#include <xmmintrin.h>

#include <wayland-client.h>

#include "simd.h"
#include "init.h"
#include "state-util.h"


#define RGBA32_PIXEL_STRIDE 4

// TODO: Check stride performance on other systems (tested on 5600h)
static_assert(sizeof(__m128i) % RGBA32_PIXEL_STRIDE == 0, "sizeof(__m128i) is not divisible by RGBA32_PIXEL_STRIDE");
#define PIXELS_PER_M128I (sizeof(__m128i) / RGBA32_PIXEL_STRIDE)
#define SSE_ROW_STRIDE 4
#define FALLBACK_STRIDE_PX 4

#define _INLINE __attribute__((always_inline))

#define _TARGET_SSE41 \
    __attribute__((optimize("O3"))) \
    __attribute__((target("sse4.1")))
// TODO: Make _FALLBACK more robust ?
#define _TARGET_FALLBACK \
    __attribute__((optimize("O3"))) \
    __attribute__((optimize("no-tree-vectorize"))) \
    __attribute__((target("no-sse")))


static inline void *
_floor_align_pointer_sse41(const void *ptr)
{
    const size_t delta = (uintptr_t)ptr & 0x0F;
    void *const ptr_aligned = (char *)ptr - delta;

    assert((uintptr_t)ptr_aligned % SSE_ALIGNMENT_BYTES == 0);

    return ptr_aligned;
}

static inline int
_ceil_align_bytes_sse41(int value)
{
    const int floor_delta = value & 0x0F;

    assert(floor_delta < SSE_ALIGNMENT_BYTES);
    const int value_aligned = floor_delta == 0 ? value : value + SSE_ALIGNMENT_BYTES - floor_delta;
    assert(value_aligned % SSE_ALIGNMENT_BYTES == 0);

    return value_aligned;
}

// dimension => width or height
static inline int
_pad_align_src_width_px_sse41(
    int width_px,
    int pixel_stride,
    const void *src_original,
    const void *src_aligned
) {
    ptrdiff_t src_delta = (char *)src_original - (char *)src_aligned;

    int width_bytes_aligned;
    assert(src_delta % pixel_stride == 0);
    width_bytes_aligned = width_px * pixel_stride + src_delta;
    width_bytes_aligned = _ceil_align_bytes_sse41(width_bytes_aligned);
    assert(width_bytes_aligned % SSE_ALIGNMENT_BYTES == 0);

    assert(width_bytes_aligned % pixel_stride == 0);
    int width_px_aligned = width_bytes_aligned / pixel_stride;

    return width_px_aligned;
}

static inline int
_pad_align_src_height_px_sse41(
    int height_px,
    int pixel_stride
) {
    // XXX: Just unroll this manually later probably
    int height_bytes_aligned = _ceil_align_bytes_sse41(height_px * pixel_stride);

    assert(height_bytes_aligned % pixel_stride == 0);
    int height_px_aligned = height_bytes_aligned / pixel_stride;

    return height_px_aligned;
}


_INLINE _TARGET_FALLBACK
static inline uint32_t
_fallback_convert_pixel_format(
    uint32_t pixel,
    uint32_t rgba_shift_mask // NOTE: NOT Shuffle mask.
) {
    uint32_t converted_pixel =
             ((pixel & 0xFF000000) >> 24) << ((rgba_shift_mask & 0xFF000000) >> 24)
           | ((pixel & 0x00FF0000) >> 16) << ((rgba_shift_mask & 0x00FF0000) >> 16)
           | ((pixel & 0x0000FF00) >> 8)  << ((rgba_shift_mask & 0x0000FF00) >> 8)
           | ((pixel & 0x000000FF))       << ((rgba_shift_mask & 0x000000FF))
    ;

    return converted_pixel;
}

_TARGET_FALLBACK
static void
transform_framebuffer_fallback(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle_mask,
    enum wl_output_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {
    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)

    assert(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = get_transformed_width(src_width_px, src_height_px, transform);
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;
    *dst_with_offset = dst;

    const int dst_height_px = get_transformed_height(src_width_px, src_height_px, transform);
    const int dst_width_px = get_transformed_width(src_width_px, src_height_px, transform);

    const uint32_t rgba_shift_mask = rgba_shuffle_mask * 8;

    // XXX TODO: Implement flipped
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_FLIPPED:
    case WL_OUTPUT_TRANSFORM_NORMAL:
        {
            static const int tile_height = FALLBACK_STRIDE_PX;
            // XXX: Keeping tile_width for testing despite optimal seems to be 1
            //      (on a 5600h, both with and without auto-vectorization)
            static const int tile_width = 1;

            for (int y = 0; y < src_height_px; y += tile_height) {
                for (int x = 0; x < src_width_px; x += tile_width) {

                    for (int _y = 0; _y < tile_height; ++_y) {
                        for (int _x = 0; _x < tile_width; ++_x) {

                            const char *const _src = (char *)src
                                + (y + _y) * src_stride_bytes
                                + (x + _x) * RGBA32_PIXEL_STRIDE;

                            char *const _dst = (char *)dst
                                + (y + _y) * dst_stride_bytes
                                + (x + _x) * RGBA32_PIXEL_STRIDE;

                            uint32_t val = *(uint32_t *)_src;
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }
        }
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
    case WL_OUTPUT_TRANSFORM_180:
        {
            static const int tile_height = FALLBACK_STRIDE_PX;
            // XXX: Keeping tile_width for testing despite optimal seems to be 1
            //      (on a 5600h, both with and without auto-vectorization)
            static const int tile_width = 1;

            // XXX: This assumes a tile_width of 1 (doesn't ensure padding/alignment)
            char *const dst_last_pixel_address =
                (char *)dst
                + (dst_height_px - 1) * dst_stride_bytes
                + (dst_width_px - 1) * RGBA32_PIXEL_STRIDE
            ;


            for (int y = 0; y < src_height_px; y += tile_height) {
                for (int x = 0; x < src_width_px; x += tile_width) {

                    for (int _y = 0; _y < tile_height; ++_y) {
                        for (int _x = 0; _x < tile_width; ++_x) {

                            const char *const _src = (char *)src
                                + (y + _y) * src_stride_bytes
                                + (x + _x) * RGBA32_PIXEL_STRIDE;

                            char *const _dst = (char *)dst_last_pixel_address
                                - (y + _y) * dst_stride_bytes
                                - (x + _x) * RGBA32_PIXEL_STRIDE;

                            uint32_t val = *(uint32_t *)_src;
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }
        }
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_90:
        {
            const int dst_x_px_max = dst_width_px - 1;

            static const int tile_height = FALLBACK_STRIDE_PX;
            // XXX: Keeping tile_width for testing despite optimal seems to be 1
            //      (on a 5600h, both with and without auto-vectorization)
            static const int tile_width = 1;

            for (int y = 0; y < src_height_px; y += tile_height) {
                for (int x = 0; x < src_width_px; x += tile_width) {

                    for (int _y = 0; _y < tile_height; ++_y) {
                        for (int _x = 0; _x < tile_width; ++_x) {

                            const char *const _src = (char *)src
                                         + (y + _y) * src_stride_bytes
                                         + (x + _x) * RGBA32_PIXEL_STRIDE;

                            // NOTE: Rotation-specific (90 vs 270)
                            char *const _dst = (char *)dst
                                         + dst_x_px_max - (y + _y) * RGBA32_PIXEL_STRIDE
                                         + (x + _x) * dst_stride_bytes;

                            uint32_t val = *(uint32_t *)_src;
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }
        }
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
    case WL_OUTPUT_TRANSFORM_270:
        {
            const int dst_y_px_max = dst_height_px - 1;

            static const int tile_height = FALLBACK_STRIDE_PX;
            // XXX: Keeping tile_width for testing despite optimal seems to be 1
            //      (on a 5600h, both with and without auto-vectorization)
            static const int tile_width = 1;

            for (int y = 0; y < src_height_px; y += tile_height) {
                for (int x = 0; x < src_width_px; x += tile_width) {

                    for (int _y = 0; _y < tile_height; ++_y) {
                        for (int _x = 0; _x < tile_width; ++_x) {

                            const char *const _src = (char *)src
                                         + (y + _y) * src_stride_bytes
                                         + (x + _x) * RGBA32_PIXEL_STRIDE;

                            // NOTE: Rotation-specific
                            char *const _dst = (char *)dst
                                         + (y + _y) * RGBA32_PIXEL_STRIDE
                                         + (dst_y_px_max - (x + _x)) * dst_stride_bytes;

                            uint32_t val = *(uint32_t *)_src;
                            val = _fallback_convert_pixel_format(val, rgba_shift_mask);

                            *(uint32_t *)_dst = val;
                        }
                    }

                }
            }

        }
        break;
    }
}


// TODO: Use SSE_ROW_STRIDE for an unrolled loop in these for easier tweaking
_TARGET_SSE41 _INLINE
static inline void
_sse41_load_rows_unaligned(
    __m128i rows[SSE_ROW_STRIDE],
    const __m128i *row_addrs[SSE_ROW_STRIDE]
) {
    rows[0] = _mm_loadu_si128(row_addrs[0]);
    rows[1] = _mm_loadu_si128(row_addrs[1]);
    rows[2] = _mm_loadu_si128(row_addrs[2]);
    rows[3] = _mm_loadu_si128(row_addrs[3]);
}

_TARGET_SSE41 _INLINE
static inline void
_sse41_store_rows_unaligned(
    __m128i rows[SSE_ROW_STRIDE],
    __m128i *row_addrs[SSE_ROW_STRIDE]
) {
    _mm_storeu_si128(row_addrs[0], rows[0]);
    _mm_storeu_si128(row_addrs[1], rows[1]);
    _mm_storeu_si128(row_addrs[2], rows[2]);
    _mm_storeu_si128(row_addrs[3], rows[3]);
}

_TARGET_SSE41 _INLINE
static inline void
_sse41_get_src_row_addresses(
    const __m128i *row_addrs[SSE_ROW_STRIDE],
    const char *row_addr_0,
    int src_stride_bytes
) {
    row_addrs[0] = (__m128i *)(row_addr_0);
    row_addrs[1] = (__m128i *)(row_addr_0 + 1 * src_stride_bytes);
    row_addrs[2] = (__m128i *)(row_addr_0 + 2 * src_stride_bytes);
    row_addrs[3] = (__m128i *)(row_addr_0 + 3 * src_stride_bytes);
}

_TARGET_SSE41 _INLINE
static inline void
_sse41_get_dst_row_addresses(
    __m128i *row_addrs[SSE_ROW_STRIDE],
    const char *row_addr_0,
    int dst_stride_bytes
) {
    row_addrs[0] = (__m128i *)(row_addr_0);
    row_addrs[1] = (__m128i *)(row_addr_0 + 1 * dst_stride_bytes);
    row_addrs[2] = (__m128i *)(row_addr_0 + 2 * dst_stride_bytes);
    row_addrs[3] = (__m128i *)(row_addr_0 + 3 * dst_stride_bytes);
}

_TARGET_SSE41 _INLINE
static inline void
_sse41_convert_pixel_format(
    __m128i rows[SSE_ROW_STRIDE],
    __m128i shuffle_mask
) {
    rows[0] = _mm_shuffle_epi8(rows[0], shuffle_mask);
    rows[1] = _mm_shuffle_epi8(rows[1], shuffle_mask);
    rows[2] = _mm_shuffle_epi8(rows[2], shuffle_mask);
    rows[3] = _mm_shuffle_epi8(rows[3], shuffle_mask);
}

_TARGET_SSE41 _INLINE
static inline void
_sse41_rotate_270(
    __m128i src_rows[SSE_ROW_STRIDE],
    __m128i dst_rows[SSE_ROW_STRIDE]
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

_TARGET_SSE41 _INLINE
static inline __m128i
_sse41_rotate_180_get_modified_rgba_shuffle(
    const __m128i original_rgba_shuffle_mask
) {
    return _mm_shuffle_epi32(original_rgba_shuffle_mask, _MM_SHUFFLE(0,1,2,3));
}

_TARGET_SSE41 _INLINE
static inline void
_sse41_rotate_90(
    __m128i src_rows[SSE_ROW_STRIDE],
    __m128i dst_rows[SSE_ROW_STRIDE]
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

// SSE41 TODOs:
// - Prefetch? Tiling? Seems to end up neutral or worse compared to naive implementation.
//

_TARGET_SSE41
static void
transform_framebuffer__sse41_unaligned__rotate_270(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    __m128i rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i src_block_rows[SSE_ROW_STRIDE] = { };
    const __m128i *src_block_row_addrs[SSE_ROW_STRIDE] = { };

    __m128i dst_block_rows[SSE_ROW_STRIDE] = { };
    __m128i *dst_block_row_addrs[SSE_ROW_STRIDE] = { };


    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += SSE_ROW_STRIDE) {
        const int dst_col_px = src_row_px; // NOTE: Rotation-speicific
        assert(RGBA32_PIXEL_STRIDE * (dst_col_px + PIXELS_PER_M128I) <= dst_stride_bytes); // Stay within padded bounds
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
            // TODO: assert(dst_row_px + SSE_ROW_STRIDE <= dst_height_px); // Stay within padded bounds

            const char *const _src_block_row_addr_0 = src_block_row_addrs_base + src_col_px * RGBA32_PIXEL_STRIDE;

            _sse41_get_src_row_addresses(src_block_row_addrs, _src_block_row_addr_0, src_stride_bytes);
            _sse41_load_rows_unaligned(src_block_rows, src_block_row_addrs);
            _sse41_convert_pixel_format (src_block_rows, rgba32_shuffle_mask_128);

            {
                _sse41_rotate_270(src_block_rows, dst_block_rows); // NOTE: Rotation-specific

                _sse41_get_dst_row_addresses(dst_block_row_addrs, dst_block_row_addr_0, dst_stride_bytes);
                dst_block_row_addr_0 -= dst_stride_bytes * PIXELS_PER_M128I; // NOTE: Rotation-specific
            }

            _sse41_store_rows_unaligned(dst_block_rows, dst_block_row_addrs);
        }
    }
}

// XXX TODO: Double-check the padding and alignment for this
_TARGET_SSE41
static void
transform_framebuffer__sse41_unaligned__rotate_180(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    __m128i rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    // NOTE: Rotation-specific:
    rgba32_shuffle_mask_128 = _sse41_rotate_180_get_modified_rgba_shuffle(rgba32_shuffle_mask_128);

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

_TARGET_SSE41
static void
transform_framebuffer__sse41_unaligned__rotate_90(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    __m128i rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i src_block_rows[SSE_ROW_STRIDE] = { };
    const __m128i *src_block_row_addrs[SSE_ROW_STRIDE] = { };

    __m128i dst_block_rows[SSE_ROW_STRIDE] = { };
    __m128i *dst_block_row_addrs[SSE_ROW_STRIDE] = { };


    for (int src_row_px = 0; src_row_px < src_height_px; src_row_px += SSE_ROW_STRIDE) {
        // NOTE: Rotation-specific code:
        const int dst_col_px = (src_height_px - 4) - src_row_px; // -4 => len -> index
        assert(RGBA32_PIXEL_STRIDE * (dst_col_px + PIXELS_PER_M128I) <= src_stride_bytes); // Stay within padded bounds
        const int dst_col_offset_bytes = dst_col_px * RGBA32_PIXEL_STRIDE;
        char *dst_block_row_addr_0 = (char *)dst
                                     + dst_col_offset_bytes;

        const char *const src_block_row_addrs_base = (char *)src + src_row_px * src_stride_bytes;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += PIXELS_PER_M128I) {
            // TODO: assert(dst_row_px + SSE_ROW_STRIDE <= dst_height_px); // Stay within padded bounds

            const char *const _src_block_row_addr_0 = src_block_row_addrs_base + src_col_px * RGBA32_PIXEL_STRIDE;

            _sse41_get_src_row_addresses(src_block_row_addrs, _src_block_row_addr_0, src_stride_bytes);
            _sse41_load_rows_unaligned(src_block_rows, src_block_row_addrs);
            _sse41_convert_pixel_format(src_block_rows, rgba32_shuffle_mask_128);

            {
                // NOTE: Rotation-specific code
                _sse41_rotate_90(src_block_rows, dst_block_rows);
                _sse41_get_dst_row_addresses(dst_block_row_addrs, dst_block_row_addr_0, dst_stride_bytes);
                dst_block_row_addr_0 += dst_stride_bytes * PIXELS_PER_M128I;
            }

            _sse41_store_rows_unaligned(dst_block_rows, dst_block_row_addrs);
        }
    }
}

_TARGET_SSE41
static void
transform_framebuffer__sse41_unaligned__rotate_0(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    __m128i rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    __m128i *dst_curr = (__m128i *)dst;
    const __m128i *src_curr = src;

    for (int src_row_px = 0; src_row_px < src_height_px; ++src_row_px) {
        const __m128i *dst_row_base = dst_curr;
        const __m128i *src_row_base = src_curr;

        for (int src_col_px = 0; src_col_px < src_width_px; src_col_px += PIXELS_PER_M128I) {
            // TODO: assert(RGBA32_PIXEL_STRIDE * (dst_col_px + PIXELS_PER_M128I) <= dst_stride_bytes); // Stay within padded bounds

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

typedef void (*transform_framebuffer_fn__sse41)(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    void *const restrict dst,
    const int dst_stride_bytes,
    __m128i rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
);

_TARGET_SSE41
static void
_transform_framebuffer_sse41__unaligned(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle_mask,
    enum wl_output_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {
    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)
    const __m128i _rgba_shuffle_mask_128_offsets = _mm_setr_epi8(0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12);
    const __m128i _rgba_shuffle_mask_128 = _mm_set1_epi32(rgba_shuffle_mask);
    const __m128i rgba_shuffle_mask_128 = _mm_add_epi8(_rgba_shuffle_mask_128_offsets, _rgba_shuffle_mask_128);

    assert(src_width_px * RGBA32_PIXEL_STRIDE <= src_stride_bytes);
    const int _dst_stride_px = get_transformed_width(src_width_px, src_height_px, transform);
    // XXX: This is not needed for unaligned
    const int dst_stride_bytes = RGBA32_PIXEL_STRIDE * _dst_stride_px;
    *dst_stride = dst_stride_bytes;
    *dst_with_offset = dst;

    transform_framebuffer_fn__sse41 transform_fn = NULL;

    switch (transform) {
    case WL_OUTPUT_TRANSFORM_270:
        transform_fn = transform_framebuffer__sse41_unaligned__rotate_270; break;
    case WL_OUTPUT_TRANSFORM_180:
        transform_fn = transform_framebuffer__sse41_unaligned__rotate_180; break;
    case WL_OUTPUT_TRANSFORM_90:
        transform_fn = transform_framebuffer__sse41_unaligned__rotate_90;  break;
    case WL_OUTPUT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer__sse41_unaligned__rotate_0;  break;
    default:
        // XXX TODO: Implement flipped
        transform_framebuffer_fallback(
                src, dst,
                src_width_px, src_height_px, src_stride_bytes,
                rgba_shuffle_mask, transform,
                dst_with_offset, dst_stride
        );
        return;
    }

    assert(transform_fn != NULL);
    transform_fn(
        src,
        src_width_px,
        src_height_px,
        src_stride_bytes,
        dst,
        dst_stride_bytes,
        rgba_shuffle_mask_128
    );
}


// TODO: Consider adding aligned and/or streamed versions of the sse functions
//           Initial testing did not show a significant difference for simple
//           image capture, on a 5600h CPU. Not tested for video, since we'll
//           probably just stick to swscale/libav* filters.

typedef void (*transform_framebuffer_fn)(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorders dst's pixel byte-order relative to src.
    //   8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum wl_output_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
);

// Rotates frame buffer, shuffles pixel geometry, and stores result to dst
//
// NOTE: If either of [src, width, height] are not divisible by the
// SIMD-required alignment, then this function will assume that we have enough
// available padding in *both directions* for all of them.
//     `dst` must be already aligned
void
transform_framebuffer(
    const void *src,
    void *dst,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    // Reorder src pixel byte-order before moving to dst
    // 8-bit-valued mask representing new order
    //     I.e. 0x03000201 => 3, 0, 2, 1 => (RGBA -> ARBG)
    uint32_t rgba_shuffle,
    enum wl_output_transform transform,
    void **dst_with_offset,
    uintptr_t *dst_stride
) {
    __builtin_cpu_init();

    transform_framebuffer_fn selected_function;

    if (__builtin_cpu_supports("sse4.1")) {
        // selected_function = _transform_framebuffer_sse41;
        selected_function = _transform_framebuffer_sse41__unaligned;
    } else {
        selected_function = transform_framebuffer_fallback;
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

