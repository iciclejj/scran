#ifndef SCRANROT_SSE_H
#define SCRANROT_SSE_H


#include <emmintrin.h>

#include "../include/scranrot.h"


_Static_assert(sizeof(__m128i) % RGBA32_PIXEL_STRIDE == 0, "sizeof(__m128i) is not divisible by RGBA32_PIXEL_STRIDE");
#define PIXELS_PER_M128I ((int)sizeof(__m128i) / RGBA32_PIXEL_STRIDE)


static inline __m128i
scranrot_sse2_rgba_shuffle_to_m128i(uint32_t rgba_shuffle_mask) {
    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)
    const __m128i _rgba_shuffle_mask_128_offsets = _mm_setr_epi8(0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12);
    const __m128i _rgba_shuffle_mask_128 = _mm_set1_epi32(rgba_shuffle_mask);
    const __m128i rgba_shuffle_mask_128 = _mm_add_epi8(_rgba_shuffle_mask_128_offsets, _rgba_shuffle_mask_128);
    return rgba_shuffle_mask_128;
}

SCRANROT_ALWAYS_INLINE
static inline __m128i
scranrot_sse2_rotate_180_get_modified_rgba_shuffle(const __m128i original_rgba_shuffle_mask) {
    return _mm_shuffle_epi32(original_rgba_shuffle_mask, _MM_SHUFFLE(0,1,2,3));
}


#endif
