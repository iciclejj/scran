#ifndef SCRANROT_SSE_H
#define SCRANROT_SSE_H


#include <emmintrin.h>

#include "./implementations.h"


static inline __m128i SCRANROT_TARGET_SSE2 SCRANROT_ALWAYS_INLINE
scranrot_sse2_loadu_m128i(const void *src)
{
    return _mm_loadu_si128((const __m128i_u *)src);
}

static inline void SCRANROT_TARGET_SSE2 SCRANROT_ALWAYS_INLINE
scranrot_sse2_storeu_m128i(void *dst, __m128i val)
{
    _mm_storeu_si128((__m128i_u *)dst, val);
}


static inline void SCRANROT_TARGET_SSE2 SCRANROT_ALWAYS_INLINE
scranrot_sse2_transpose_inplace_16x16_8bpp(
    __m128i arg[16]
) {
    __m128i tmp[16];

    tmp[ 0] = _mm_unpacklo_epi8(arg[ 0], arg[ 1]);
    tmp[ 1] = _mm_unpackhi_epi8(arg[ 0], arg[ 1]);
    tmp[ 2] = _mm_unpacklo_epi8(arg[ 2], arg[ 3]);
    tmp[ 3] = _mm_unpackhi_epi8(arg[ 2], arg[ 3]);
    tmp[ 4] = _mm_unpacklo_epi8(arg[ 4], arg[ 5]);
    tmp[ 5] = _mm_unpackhi_epi8(arg[ 4], arg[ 5]);
    tmp[ 6] = _mm_unpacklo_epi8(arg[ 6], arg[ 7]);
    tmp[ 7] = _mm_unpackhi_epi8(arg[ 6], arg[ 7]);
    tmp[ 8] = _mm_unpacklo_epi8(arg[ 8], arg[ 9]);
    tmp[ 9] = _mm_unpackhi_epi8(arg[ 8], arg[ 9]);
    tmp[10] = _mm_unpacklo_epi8(arg[10], arg[11]);
    tmp[11] = _mm_unpackhi_epi8(arg[10], arg[11]);
    tmp[12] = _mm_unpacklo_epi8(arg[12], arg[13]);
    tmp[13] = _mm_unpackhi_epi8(arg[12], arg[13]);
    tmp[14] = _mm_unpacklo_epi8(arg[14], arg[15]);
    tmp[15] = _mm_unpackhi_epi8(arg[14], arg[15]);

    arg[ 0] = _mm_unpacklo_epi16(tmp[ 0], tmp[ 2]);
    arg[ 1] = _mm_unpackhi_epi16(tmp[ 0], tmp[ 2]);
    arg[ 2] = _mm_unpacklo_epi16(tmp[ 1], tmp[ 3]);
    arg[ 3] = _mm_unpackhi_epi16(tmp[ 1], tmp[ 3]);
    arg[ 4] = _mm_unpacklo_epi16(tmp[ 4], tmp[ 6]);
    arg[ 5] = _mm_unpackhi_epi16(tmp[ 4], tmp[ 6]);
    arg[ 6] = _mm_unpacklo_epi16(tmp[ 5], tmp[ 7]);
    arg[ 7] = _mm_unpackhi_epi16(tmp[ 5], tmp[ 7]);
    arg[ 8] = _mm_unpacklo_epi16(tmp[ 8], tmp[10]);
    arg[ 9] = _mm_unpackhi_epi16(tmp[ 8], tmp[10]);
    arg[10] = _mm_unpacklo_epi16(tmp[ 9], tmp[11]);
    arg[11] = _mm_unpackhi_epi16(tmp[ 9], tmp[11]);
    arg[12] = _mm_unpacklo_epi16(tmp[12], tmp[14]);
    arg[13] = _mm_unpackhi_epi16(tmp[12], tmp[14]);
    arg[14] = _mm_unpacklo_epi16(tmp[13], tmp[15]);
    arg[15] = _mm_unpackhi_epi16(tmp[13], tmp[15]);

    tmp[ 0] = _mm_unpacklo_epi32(arg[ 0], arg[ 4]);
    tmp[ 1] = _mm_unpackhi_epi32(arg[ 0], arg[ 4]);
    tmp[ 2] = _mm_unpacklo_epi32(arg[ 1], arg[ 5]);
    tmp[ 3] = _mm_unpackhi_epi32(arg[ 1], arg[ 5]);
    tmp[ 4] = _mm_unpacklo_epi32(arg[ 2], arg[ 6]);
    tmp[ 5] = _mm_unpackhi_epi32(arg[ 2], arg[ 6]);
    tmp[ 6] = _mm_unpacklo_epi32(arg[ 3], arg[ 7]);
    tmp[ 7] = _mm_unpackhi_epi32(arg[ 3], arg[ 7]);
    tmp[ 8] = _mm_unpacklo_epi32(arg[ 8], arg[12]);
    tmp[ 9] = _mm_unpackhi_epi32(arg[ 8], arg[12]);
    tmp[10] = _mm_unpacklo_epi32(arg[ 9], arg[13]);
    tmp[11] = _mm_unpackhi_epi32(arg[ 9], arg[13]);
    tmp[12] = _mm_unpacklo_epi32(arg[10], arg[14]);
    tmp[13] = _mm_unpackhi_epi32(arg[10], arg[14]);
    tmp[14] = _mm_unpacklo_epi32(arg[11], arg[15]);
    tmp[15] = _mm_unpackhi_epi32(arg[11], arg[15]);

    // Last pass reversed relative to transpose
    arg[0]  = _mm_unpacklo_epi64(tmp[ 0], tmp[ 8]);
    arg[1]  = _mm_unpackhi_epi64(tmp[ 0], tmp[ 8]);
    arg[2]  = _mm_unpacklo_epi64(tmp[ 1], tmp[ 9]);
    arg[3]  = _mm_unpackhi_epi64(tmp[ 1], tmp[ 9]);
    arg[4]  = _mm_unpacklo_epi64(tmp[ 2], tmp[10]);
    arg[5]  = _mm_unpackhi_epi64(tmp[ 2], tmp[10]);
    arg[6]  = _mm_unpacklo_epi64(tmp[ 3], tmp[11]);
    arg[7]  = _mm_unpackhi_epi64(tmp[ 3], tmp[11]);
    arg[8]  = _mm_unpacklo_epi64(tmp[ 4], tmp[12]);
    arg[9]  = _mm_unpackhi_epi64(tmp[ 4], tmp[12]);
    arg[10] = _mm_unpacklo_epi64(tmp[ 5], tmp[13]);
    arg[11] = _mm_unpackhi_epi64(tmp[ 5], tmp[13]);
    arg[12] = _mm_unpacklo_epi64(tmp[ 6], tmp[14]);
    arg[13] = _mm_unpackhi_epi64(tmp[ 6], tmp[14]);
    arg[14] = _mm_unpacklo_epi64(tmp[ 7], tmp[15]);
    arg[15] = _mm_unpackhi_epi64(tmp[ 7], tmp[15]);
}
// Transpose is equivalent to 270 degree rotation + flip.
//   We can simply read the result in reverse order in order to get the 270.
//   Reading in reverse order is often simpler for rotating the entire image
//   anyways, depending on how we do the pointer walks. At least it is simpler
//   inside of our loops here.
#define scranrot_sse2_rotate_270_inplace_16x16_8bpp scranrot_sse2_transpose_inplace_16x16_8bpp



static inline void SCRANROT_TARGET_SSE2 SCRANROT_ALWAYS_INLINE
scranrot_sse2_rotate_90_inplace_16x16_8bpp(
    __m128i arg[16]
) {
    __m128i tmp[16];

    // First pass reversed relative to transpose
    tmp[ 0] = _mm_unpacklo_epi8(arg[15], arg[14]);
    tmp[ 1] = _mm_unpackhi_epi8(arg[15], arg[14]);
    tmp[ 2] = _mm_unpacklo_epi8(arg[13], arg[12]);
    tmp[ 3] = _mm_unpackhi_epi8(arg[13], arg[12]);
    tmp[ 4] = _mm_unpacklo_epi8(arg[11], arg[10]);
    tmp[ 5] = _mm_unpackhi_epi8(arg[11], arg[10]);
    tmp[ 6] = _mm_unpacklo_epi8(arg[ 9], arg[ 8]);
    tmp[ 7] = _mm_unpackhi_epi8(arg[ 9], arg[ 8]);
    tmp[ 8] = _mm_unpacklo_epi8(arg[ 7], arg[ 6]);
    tmp[ 9] = _mm_unpackhi_epi8(arg[ 7], arg[ 6]);
    tmp[10] = _mm_unpacklo_epi8(arg[ 5], arg[ 4]);
    tmp[11] = _mm_unpackhi_epi8(arg[ 5], arg[ 4]);
    tmp[12] = _mm_unpacklo_epi8(arg[ 3], arg[ 2]);
    tmp[13] = _mm_unpackhi_epi8(arg[ 3], arg[ 2]);
    tmp[14] = _mm_unpacklo_epi8(arg[ 1], arg[ 0]);
    tmp[15] = _mm_unpackhi_epi8(arg[ 1], arg[ 0]);

    arg[0]  = _mm_unpacklo_epi16(tmp[ 0], tmp[ 2]);
    arg[1]  = _mm_unpackhi_epi16(tmp[ 0], tmp[ 2]);
    arg[2]  = _mm_unpacklo_epi16(tmp[ 1], tmp[ 3]);
    arg[3]  = _mm_unpackhi_epi16(tmp[ 1], tmp[ 3]);
    arg[4]  = _mm_unpacklo_epi16(tmp[ 4], tmp[ 6]);
    arg[5]  = _mm_unpackhi_epi16(tmp[ 4], tmp[ 6]);
    arg[6]  = _mm_unpacklo_epi16(tmp[ 5], tmp[ 7]);
    arg[7]  = _mm_unpackhi_epi16(tmp[ 5], tmp[ 7]);
    arg[8]  = _mm_unpacklo_epi16(tmp[ 8], tmp[10]);
    arg[9]  = _mm_unpackhi_epi16(tmp[ 8], tmp[10]);
    arg[10] = _mm_unpacklo_epi16(tmp[ 9], tmp[11]);
    arg[11] = _mm_unpackhi_epi16(tmp[ 9], tmp[11]);
    arg[12] = _mm_unpacklo_epi16(tmp[12], tmp[14]);
    arg[13] = _mm_unpackhi_epi16(tmp[12], tmp[14]);
    arg[14] = _mm_unpacklo_epi16(tmp[13], tmp[15]);
    arg[15] = _mm_unpackhi_epi16(tmp[13], tmp[15]);

    tmp[ 0] = _mm_unpacklo_epi32(arg[ 0], arg[ 4]);
    tmp[ 1] = _mm_unpackhi_epi32(arg[ 0], arg[ 4]);
    tmp[ 2] = _mm_unpacklo_epi32(arg[ 1], arg[ 5]);
    tmp[ 3] = _mm_unpackhi_epi32(arg[ 1], arg[ 5]);
    tmp[ 4] = _mm_unpacklo_epi32(arg[ 2], arg[ 6]);
    tmp[ 5] = _mm_unpackhi_epi32(arg[ 2], arg[ 6]);
    tmp[ 6] = _mm_unpacklo_epi32(arg[ 3], arg[ 7]);
    tmp[ 7] = _mm_unpackhi_epi32(arg[ 3], arg[ 7]);
    tmp[ 8] = _mm_unpacklo_epi32(arg[ 8], arg[12]);
    tmp[ 9] = _mm_unpackhi_epi32(arg[ 8], arg[12]);
    tmp[10] = _mm_unpacklo_epi32(arg[ 9], arg[13]);
    tmp[11] = _mm_unpackhi_epi32(arg[ 9], arg[13]);
    tmp[12] = _mm_unpacklo_epi32(arg[10], arg[14]);
    tmp[13] = _mm_unpackhi_epi32(arg[10], arg[14]);
    tmp[14] = _mm_unpacklo_epi32(arg[11], arg[15]);
    tmp[15] = _mm_unpackhi_epi32(arg[11], arg[15]);

    arg[ 0] = _mm_unpacklo_epi64(tmp[ 0], tmp[ 8]);
    arg[ 1] = _mm_unpackhi_epi64(tmp[ 0], tmp[ 8]);
    arg[ 2] = _mm_unpacklo_epi64(tmp[ 1], tmp[ 9]);
    arg[ 3] = _mm_unpackhi_epi64(tmp[ 1], tmp[ 9]);
    arg[ 4] = _mm_unpacklo_epi64(tmp[ 2], tmp[10]);
    arg[ 5] = _mm_unpackhi_epi64(tmp[ 2], tmp[10]);
    arg[ 6] = _mm_unpacklo_epi64(tmp[ 3], tmp[11]);
    arg[ 7] = _mm_unpackhi_epi64(tmp[ 3], tmp[11]);
    arg[ 8] = _mm_unpacklo_epi64(tmp[ 4], tmp[12]);
    arg[ 9] = _mm_unpackhi_epi64(tmp[ 4], tmp[12]);
    arg[10] = _mm_unpacklo_epi64(tmp[ 5], tmp[13]);
    arg[11] = _mm_unpackhi_epi64(tmp[ 5], tmp[13]);
    arg[12] = _mm_unpacklo_epi64(tmp[ 6], tmp[14]);
    arg[13] = _mm_unpackhi_epi64(tmp[ 6], tmp[14]);
    arg[14] = _mm_unpacklo_epi64(tmp[ 7], tmp[15]);
    arg[15] = _mm_unpackhi_epi64(tmp[ 7], tmp[15]);
}


static inline __m128i
scranrot_sse2_rgba_shuffle_to_m128i(uint32_t rgba_shuffle_mask) {
    // TODO: Assert rgba_shuffle is valid (and let (0 => 0,1,2,3) ?)
    const __m128i _rgba_shuffle_mask_128_offsets = _mm_setr_epi8(0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12);
    const __m128i _rgba_shuffle_mask_128 = _mm_set1_epi32(rgba_shuffle_mask);
    const __m128i rgba_shuffle_mask_128 = _mm_add_epi8(_rgba_shuffle_mask_128_offsets, _rgba_shuffle_mask_128);
    return rgba_shuffle_mask_128;
}

static inline __m128i SCRANROT_ALWAYS_INLINE
scranrot_sse2_rotate_180_get_modified_rgba_shuffle(const __m128i original_rgba_shuffle_mask) {
    return _mm_shuffle_epi32(original_rgba_shuffle_mask, _MM_SHUFFLE(0,1,2,3));
}


#endif
