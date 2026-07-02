#if defined(__x86_64__) || defined(__i386__)


#include <stdbool.h>
#include <stdint.h>
#include <immintrin.h>

#include "scranrot.h"
#include "../util.h"
#include "../generic-kernel-dispatcher.h"
#include "../implementations.h"


enum {
    RGBA32_PIXELS_PER_YMM = 8,

    KERNEL_TILE_WIDTH_PX  = 64,
    KERNEL_TILE_HEIGHT_PX = 64,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};
_Static_assert(RGBA32_PIXELS_PER_YMM * RGBA32_PIXEL_STRIDE == sizeof(__m256i), "This file assumes a YMM register holds 8 RGBA32 pixels.");


static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
loadu_m256i(const void *src)
{
    return _mm256_loadu_si256((const __m256i_u *)src);
}

static inline void SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
storeu_m256i(void *dst, __m256i val)
{
    _mm256_storeu_si256((__m256i_u *)dst, val);
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
rgba32_shuffle_to_m256i(uint32_t rgba32_shuffle_mask)
{
    const __m128i offsets = _mm_setr_epi8(
        0,0,0,0, 4,4,4,4, 8,8,8,8, 12,12,12,12
    );
    const __m128i mask128 = _mm_add_epi8(_mm_set1_epi32(rgba32_shuffle_mask), offsets);
    return _mm256_broadcastsi128_si256(mask128);
}

// BT.709 Y'CbCr coefficients.
//
// NOTE: Pre-shuffling the coefficients in order to not need to shuffle the
// rgba loads benchmarks slower.
//
// Target: 55,183,19
// NOTE: R rounds 54.4 up to 55 so the coefficients sum to 257. This offsets
// the final >> 8 truncation's ~-0.5 mean error.
//
// Similar drill as in get_yuv_u_rbga_coefficients_256. (We can't simply flip
// the sign here, since 183 won't fit on either side of 0 in signed i8, and we
// need to use VPMADDUBSW, which treats one of its operands as signed (i.e.
// these coefficients) as signed.
//
// RBGA (as opposed to RGBA) simplifies the calculation, since A (Alpha) will
// always be multiplied by zero, and so does not care about the
// "pairwise coefficients" values
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
get_yuv_y_rbga_coefficients_256(void) {
    return _mm256_set1_epi32(scranrot_pack_4xU8(55, 19, 1, 0));
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
get_yuv_y_pairwise_rbga_coefficients_256(void) {
    return _mm256_setr_epi16(1, 183, 1, 183, 1, 183, 1, 183, 1, 183, 1, 183, 1, 183, 1, 183);
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
get_yuv_u_rbga_coefficients_256(void) {
    // Target (RGB): -29,-99,128.
    // b (128) cannot be represented in a signed i8, so flip the sign on it and
    // its _mm256_maddubs_epi16()-paired coefficient, then fold the resulting
    // pairwise i16 products as -(r+b)+(g+a), instead of (r+b)+(g+a).
    return _mm256_set1_epi32(scranrot_pack_4xU8(+29, -128, -99, 0));
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
get_yuv_v_rbga_coefficients_256(void) {
    // Target (RGB): 128,-116,-12.
    // See comment in get_yuv_u_rbga_coefficients_256.
    return _mm256_set1_epi32(scranrot_pack_4xU8(-128, +12, -116, 0));
}


static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
rotate_180_get_modified_rgba_shuffle_256(const __m256i original_rgba_shuffle_mask) {
    return _mm256_shuffle_epi32(original_rgba_shuffle_mask, _MM_SHUFFLE(0,1,2,3));
}


static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_pairwise_sum_i16_to_i32(__m256i val) {
    return _mm256_madd_epi16(val, _mm256_set1_epi16(1));
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
get_yuv_u_pairwise_rbga_coefficients_256(void) {
    return _mm256_setr_epi16(-1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1);
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
get_yuv_v_pairwise_rbga_coefficients_256(void) {
    return _mm256_setr_epi16(-1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1);
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_convert_packed_to_concatenation(__m256i val) {
    return _mm256_permute4x64_epi64(
        val,
        _MM_SHUFFLE(3, 1, 2, 0)
    );
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_concat_i16_to_i8(__m256i a, __m256i b) {
    return m256i_convert_packed_to_concatenation(
        _mm256_packus_epi16(a, b)
    );
}

// Fixes up the pack order all in one go so we don't need to call
// _mm256_permute4x64_epi64 after every intermediate interleaved
// pack result, if the intention is to concat-pack
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_convert_doubly_packed_to_concatenation(__m256i val) {
    return _mm256_permutevar8x32_epi32(
        val, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7)
    );
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_convert_doubly_packed_to_concatenation__laneswapped(__m256i val) {
    return _mm256_permutevar8x32_epi32(
        val, _mm256_setr_epi32(4, 0, 5, 1, 6, 2, 7, 3)
    );
}


static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_pack_i32_to_i8(__m256i a, __m256i b, __m256i c, __m256i d)
{
  return _mm256_packus_epi16(
             _mm256_packus_epi32(a, b),
             _mm256_packus_epi32(c, d)
         );
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_concat_i32_to_i8(__m256i a, __m256i b, __m256i c, __m256i d)
{
  return m256i_convert_doubly_packed_to_concatenation(
             m256i_pack_i32_to_i8(a, b, c, d)
         );
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
m256i_concat_i32_to_i8__laneswapped(__m256i a, __m256i b, __m256i c, __m256i d)
{
  return m256i_convert_doubly_packed_to_concatenation__laneswapped(
             m256i_pack_i32_to_i8(a, b, c, d)
         );
}

// Transpose/rotate two independent 16x16 byte blocks at once, one in each
// 128-bit lane. This matches AVX2's lane-local unpack semantics and lets the
// Y plane use 32-byte rows without doing four scalar-XMM transposes per tile.
static inline void SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
transpose_inplace_2x_16x16_8bpp(
    __m256i arg[16]
) {
    __m256i tmp[16];

    tmp[ 0] = _mm256_unpacklo_epi8(arg[ 0], arg[ 1]);
    tmp[ 1] = _mm256_unpackhi_epi8(arg[ 0], arg[ 1]);
    tmp[ 2] = _mm256_unpacklo_epi8(arg[ 2], arg[ 3]);
    tmp[ 3] = _mm256_unpackhi_epi8(arg[ 2], arg[ 3]);
    tmp[ 4] = _mm256_unpacklo_epi8(arg[ 4], arg[ 5]);
    tmp[ 5] = _mm256_unpackhi_epi8(arg[ 4], arg[ 5]);
    tmp[ 6] = _mm256_unpacklo_epi8(arg[ 6], arg[ 7]);
    tmp[ 7] = _mm256_unpackhi_epi8(arg[ 6], arg[ 7]);
    tmp[ 8] = _mm256_unpacklo_epi8(arg[ 8], arg[ 9]);
    tmp[ 9] = _mm256_unpackhi_epi8(arg[ 8], arg[ 9]);
    tmp[10] = _mm256_unpacklo_epi8(arg[10], arg[11]);
    tmp[11] = _mm256_unpackhi_epi8(arg[10], arg[11]);
    tmp[12] = _mm256_unpacklo_epi8(arg[12], arg[13]);
    tmp[13] = _mm256_unpackhi_epi8(arg[12], arg[13]);
    tmp[14] = _mm256_unpacklo_epi8(arg[14], arg[15]);
    tmp[15] = _mm256_unpackhi_epi8(arg[14], arg[15]);

    arg[ 0] = _mm256_unpacklo_epi16(tmp[ 0], tmp[ 2]);
    arg[ 1] = _mm256_unpackhi_epi16(tmp[ 0], tmp[ 2]);
    arg[ 2] = _mm256_unpacklo_epi16(tmp[ 1], tmp[ 3]);
    arg[ 3] = _mm256_unpackhi_epi16(tmp[ 1], tmp[ 3]);
    arg[ 4] = _mm256_unpacklo_epi16(tmp[ 4], tmp[ 6]);
    arg[ 5] = _mm256_unpackhi_epi16(tmp[ 4], tmp[ 6]);
    arg[ 6] = _mm256_unpacklo_epi16(tmp[ 5], tmp[ 7]);
    arg[ 7] = _mm256_unpackhi_epi16(tmp[ 5], tmp[ 7]);
    arg[ 8] = _mm256_unpacklo_epi16(tmp[ 8], tmp[10]);
    arg[ 9] = _mm256_unpackhi_epi16(tmp[ 8], tmp[10]);
    arg[10] = _mm256_unpacklo_epi16(tmp[ 9], tmp[11]);
    arg[11] = _mm256_unpackhi_epi16(tmp[ 9], tmp[11]);
    arg[12] = _mm256_unpacklo_epi16(tmp[12], tmp[14]);
    arg[13] = _mm256_unpackhi_epi16(tmp[12], tmp[14]);
    arg[14] = _mm256_unpacklo_epi16(tmp[13], tmp[15]);
    arg[15] = _mm256_unpackhi_epi16(tmp[13], tmp[15]);

    tmp[ 0] = _mm256_unpacklo_epi32(arg[ 0], arg[ 4]);
    tmp[ 1] = _mm256_unpackhi_epi32(arg[ 0], arg[ 4]);
    tmp[ 2] = _mm256_unpacklo_epi32(arg[ 1], arg[ 5]);
    tmp[ 3] = _mm256_unpackhi_epi32(arg[ 1], arg[ 5]);
    tmp[ 4] = _mm256_unpacklo_epi32(arg[ 2], arg[ 6]);
    tmp[ 5] = _mm256_unpackhi_epi32(arg[ 2], arg[ 6]);
    tmp[ 6] = _mm256_unpacklo_epi32(arg[ 3], arg[ 7]);
    tmp[ 7] = _mm256_unpackhi_epi32(arg[ 3], arg[ 7]);
    tmp[ 8] = _mm256_unpacklo_epi32(arg[ 8], arg[12]);
    tmp[ 9] = _mm256_unpackhi_epi32(arg[ 8], arg[12]);
    tmp[10] = _mm256_unpacklo_epi32(arg[ 9], arg[13]);
    tmp[11] = _mm256_unpackhi_epi32(arg[ 9], arg[13]);
    tmp[12] = _mm256_unpacklo_epi32(arg[10], arg[14]);
    tmp[13] = _mm256_unpackhi_epi32(arg[10], arg[14]);
    tmp[14] = _mm256_unpacklo_epi32(arg[11], arg[15]);
    tmp[15] = _mm256_unpackhi_epi32(arg[11], arg[15]);

    // Last pass reversed relative to transpose
    arg[0]  = _mm256_unpacklo_epi64(tmp[ 0], tmp[ 8]);
    arg[1]  = _mm256_unpackhi_epi64(tmp[ 0], tmp[ 8]);
    arg[2]  = _mm256_unpacklo_epi64(tmp[ 1], tmp[ 9]);
    arg[3]  = _mm256_unpackhi_epi64(tmp[ 1], tmp[ 9]);
    arg[4]  = _mm256_unpacklo_epi64(tmp[ 2], tmp[10]);
    arg[5]  = _mm256_unpackhi_epi64(tmp[ 2], tmp[10]);
    arg[6]  = _mm256_unpacklo_epi64(tmp[ 3], tmp[11]);
    arg[7]  = _mm256_unpackhi_epi64(tmp[ 3], tmp[11]);
    arg[8]  = _mm256_unpacklo_epi64(tmp[ 4], tmp[12]);
    arg[9]  = _mm256_unpackhi_epi64(tmp[ 4], tmp[12]);
    arg[10] = _mm256_unpacklo_epi64(tmp[ 5], tmp[13]);
    arg[11] = _mm256_unpackhi_epi64(tmp[ 5], tmp[13]);
    arg[12] = _mm256_unpacklo_epi64(tmp[ 6], tmp[14]);
    arg[13] = _mm256_unpackhi_epi64(tmp[ 6], tmp[14]);
    arg[14] = _mm256_unpacklo_epi64(tmp[ 7], tmp[15]);
    arg[15] = _mm256_unpackhi_epi64(tmp[ 7], tmp[15]);
}


// Rotates two independent 16x16 blocks at once
static inline void SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
rotate90_inplace_2x_16x16_8bpp(
    __m256i arg[16]
) {
    __m256i tmp[16];

    // First pass reversed relative to transpose
    tmp[ 0] = _mm256_unpacklo_epi8(arg[15], arg[14]);
    tmp[ 1] = _mm256_unpackhi_epi8(arg[15], arg[14]);
    tmp[ 2] = _mm256_unpacklo_epi8(arg[13], arg[12]);
    tmp[ 3] = _mm256_unpackhi_epi8(arg[13], arg[12]);
    tmp[ 4] = _mm256_unpacklo_epi8(arg[11], arg[10]);
    tmp[ 5] = _mm256_unpackhi_epi8(arg[11], arg[10]);
    tmp[ 6] = _mm256_unpacklo_epi8(arg[ 9], arg[ 8]);
    tmp[ 7] = _mm256_unpackhi_epi8(arg[ 9], arg[ 8]);
    tmp[ 8] = _mm256_unpacklo_epi8(arg[ 7], arg[ 6]);
    tmp[ 9] = _mm256_unpackhi_epi8(arg[ 7], arg[ 6]);
    tmp[10] = _mm256_unpacklo_epi8(arg[ 5], arg[ 4]);
    tmp[11] = _mm256_unpackhi_epi8(arg[ 5], arg[ 4]);
    tmp[12] = _mm256_unpacklo_epi8(arg[ 3], arg[ 2]);
    tmp[13] = _mm256_unpackhi_epi8(arg[ 3], arg[ 2]);
    tmp[14] = _mm256_unpacklo_epi8(arg[ 1], arg[ 0]);
    tmp[15] = _mm256_unpackhi_epi8(arg[ 1], arg[ 0]);

    arg[0]  = _mm256_unpacklo_epi16(tmp[ 0], tmp[ 2]);
    arg[1]  = _mm256_unpackhi_epi16(tmp[ 0], tmp[ 2]);
    arg[2]  = _mm256_unpacklo_epi16(tmp[ 1], tmp[ 3]);
    arg[3]  = _mm256_unpackhi_epi16(tmp[ 1], tmp[ 3]);
    arg[4]  = _mm256_unpacklo_epi16(tmp[ 4], tmp[ 6]);
    arg[5]  = _mm256_unpackhi_epi16(tmp[ 4], tmp[ 6]);
    arg[6]  = _mm256_unpacklo_epi16(tmp[ 5], tmp[ 7]);
    arg[7]  = _mm256_unpackhi_epi16(tmp[ 5], tmp[ 7]);
    arg[8]  = _mm256_unpacklo_epi16(tmp[ 8], tmp[10]);
    arg[9]  = _mm256_unpackhi_epi16(tmp[ 8], tmp[10]);
    arg[10] = _mm256_unpacklo_epi16(tmp[ 9], tmp[11]);
    arg[11] = _mm256_unpackhi_epi16(tmp[ 9], tmp[11]);
    arg[12] = _mm256_unpacklo_epi16(tmp[12], tmp[14]);
    arg[13] = _mm256_unpackhi_epi16(tmp[12], tmp[14]);
    arg[14] = _mm256_unpacklo_epi16(tmp[13], tmp[15]);
    arg[15] = _mm256_unpackhi_epi16(tmp[13], tmp[15]);

    tmp[ 0] = _mm256_unpacklo_epi32(arg[ 0], arg[ 4]);
    tmp[ 1] = _mm256_unpackhi_epi32(arg[ 0], arg[ 4]);
    tmp[ 2] = _mm256_unpacklo_epi32(arg[ 1], arg[ 5]);
    tmp[ 3] = _mm256_unpackhi_epi32(arg[ 1], arg[ 5]);
    tmp[ 4] = _mm256_unpacklo_epi32(arg[ 2], arg[ 6]);
    tmp[ 5] = _mm256_unpackhi_epi32(arg[ 2], arg[ 6]);
    tmp[ 6] = _mm256_unpacklo_epi32(arg[ 3], arg[ 7]);
    tmp[ 7] = _mm256_unpackhi_epi32(arg[ 3], arg[ 7]);
    tmp[ 8] = _mm256_unpacklo_epi32(arg[ 8], arg[12]);
    tmp[ 9] = _mm256_unpackhi_epi32(arg[ 8], arg[12]);
    tmp[10] = _mm256_unpacklo_epi32(arg[ 9], arg[13]);
    tmp[11] = _mm256_unpackhi_epi32(arg[ 9], arg[13]);
    tmp[12] = _mm256_unpacklo_epi32(arg[10], arg[14]);
    tmp[13] = _mm256_unpackhi_epi32(arg[10], arg[14]);
    tmp[14] = _mm256_unpacklo_epi32(arg[11], arg[15]);
    tmp[15] = _mm256_unpackhi_epi32(arg[11], arg[15]);

    arg[ 0] = _mm256_unpacklo_epi64(tmp[ 0], tmp[ 8]);
    arg[ 1] = _mm256_unpackhi_epi64(tmp[ 0], tmp[ 8]);
    arg[ 2] = _mm256_unpacklo_epi64(tmp[ 1], tmp[ 9]);
    arg[ 3] = _mm256_unpackhi_epi64(tmp[ 1], tmp[ 9]);
    arg[ 4] = _mm256_unpacklo_epi64(tmp[ 2], tmp[10]);
    arg[ 5] = _mm256_unpackhi_epi64(tmp[ 2], tmp[10]);
    arg[ 6] = _mm256_unpacklo_epi64(tmp[ 3], tmp[11]);
    arg[ 7] = _mm256_unpackhi_epi64(tmp[ 3], tmp[11]);
    arg[ 8] = _mm256_unpacklo_epi64(tmp[ 4], tmp[12]);
    arg[ 9] = _mm256_unpackhi_epi64(tmp[ 4], tmp[12]);
    arg[10] = _mm256_unpacklo_epi64(tmp[ 5], tmp[13]);
    arg[11] = _mm256_unpackhi_epi64(tmp[ 5], tmp[13]);
    arg[12] = _mm256_unpacklo_epi64(tmp[ 6], tmp[14]);
    arg[13] = _mm256_unpackhi_epi64(tmp[ 6], tmp[14]);
    arg[14] = _mm256_unpacklo_epi64(tmp[ 7], tmp[15]);
    arg[15] = _mm256_unpackhi_epi64(tmp[ 7], tmp[15]);
}

SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
static inline void
rotate90_inplace_32x32_8bpp(__m256i arg[static 32])
{
  // Top half:    rows  0..15.
  // Bottom half: rows 16..31.
  rotate90_inplace_2x_16x16_8bpp(&arg[0]);
  rotate90_inplace_2x_16x16_8bpp(&arg[16]);

  for (int i = 0; i < 16; ++i) {
      const __m256i top = arg[i];
      const __m256i bot = arg[i + 16];

      arg[i]      = _mm256_permute2x128_si256(bot, top, 0x20);
      arg[i + 16] = _mm256_permute2x128_si256(bot, top, 0x31);
  }
}

SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
static inline void
transpose_inplace_32x32_8bpp(__m256i arg[static 32])
{
  // Top half:    rows  0..15, lanes are TL/TR 16x16 blocks.
  // Bottom half: rows 16..31, lanes are BL/BR 16x16 blocks.
  transpose_inplace_2x_16x16_8bpp(&arg[0]);
  transpose_inplace_2x_16x16_8bpp(&arg[16]);

  for (int i = 0; i < 16; ++i) {
      const __m256i top = arg[i];       // [ transpose(TL) | transpose(TR) ]
      const __m256i bot = arg[i + 16];  // [ transpose(BL) | transpose(BR) ]

      // output rows 0..15:  [ transpose(TL) | transpose(BL) ]
      // output rows 16..31: [ transpose(TR) | transpose(BR) ]
      arg[i]      = _mm256_permute2x128_si256(top, bot, 0x20);
      arg[i + 16] = _mm256_permute2x128_si256(top, bot, 0x31);
  }
}


// TODO: Use unrolled loops for all the transpose and rotation functions.

// SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
// static inline __m256i
// _rgba32_to_yuv_plane_32bpp_unsigned_coefficients(
//     const __m256i *const rgba_in,
//     const __m256i *const coefficients,
//     const uint8_t shr
// ) {
//     return _mm256_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
//               m256i_pairwise_sum_i16_to_i32(
//                   _mm256_maddubs_epi16(*rgba_in, *coefficients)
//               ),
//               shr
//           );
// }

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_rgba32_to_yuv_y_32bpp(
    const __m256i *const rgba_in,
    const __m256i *const coefficients,
    // coefficients for adjacent byte pairs, after the VPMADDUBSW pairings
    // See get_yuv_y_rbga_coefficients_256 for more info.
    const __m256i *const pairwise_coefficients,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const uint8_t shr
) {
    return _mm256_srai_epi32(
              _mm256_madd_epi16(
                  _mm256_maddubs_epi16(*rgba_in, *coefficients),
                  *pairwise_coefficients
              ),
              shr
          );
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
    const __m256i *const rgba_in,
    const __m256i *const coefficients,
    // coefficients for adjacent byte pairs, after the VPMADDUBSW pairings
    // See get_yuv_u_rbga_coefficients_256 for more info.
    const __m256i *const pairwise_coefficients,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const uint8_t shr
) {
    // We need to represent our values as signed, so we normalize them by adding
    // the max signed absolute value, to take the (post-shr) range to 0:255.
    const __m256i uv_s_to_us_offset_epi32 = _mm256_set1_epi32((128 << 8) + 128);

    return _mm256_srai_epi32(
               _mm256_add_epi32(
                   _mm256_madd_epi16(
                       _mm256_maddubs_epi16(*rgba_in, *coefficients),
                       *pairwise_coefficients
                   ),
                   uv_s_to_us_offset_epi32
               ),
               shr
          );
}

static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp__impl(
    const __m256i rgba_in[4],
    const __m256i *const coefficients,
    const __m256i *const pairwise_coefficients,
    const uint8_t shr
) {
    return _mm256_srai_epi16(
               _mm256_packus_epi32(

                   m256i_pairwise_sum_i16_to_i32( // Sum in-between packs for precision
                       _mm256_packus_epi32(
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[0], coefficients, pairwise_coefficients, shr
                           ),
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[1], coefficients, pairwise_coefficients, shr
                           )
                       )
                   ),

                   m256i_pairwise_sum_i16_to_i32(
                       _mm256_packus_epi32(
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[2], coefficients, pairwise_coefficients, shr
                           ),
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[3], coefficients, pairwise_coefficients, shr
                           )
                       )
                   )

               ),
               1 // Divide by 2 to get averages of the madds
           );
}


static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
    const __m256i rgba_in[4],
    const __m256i *const coefficients,
    const __m256i *const pairwise_coefficients,
    const uint8_t shr
) {
    // Returns: [ (i16)(a0+a1)/2, (i16)(a2+a3)/2, ...]
    return m256i_convert_doubly_packed_to_concatenation(
        convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp__impl(
            rgba_in, coefficients, pairwise_coefficients, shr
        )
    );
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp__laneswapped(
    const __m256i rgba_in[4],
    const __m256i *const coefficients,
    const __m256i *const pairwise_coefficients,
    const uint8_t shr
) {
    // Returns: [ (i16)(a0+a1)/2, (i16)(a2+a3)/2, ...]

    return m256i_convert_doubly_packed_to_concatenation__laneswapped(
        convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp__impl(
            rgba_in, coefficients, pairwise_coefficients, shr
        )
    );
}


static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_32px_rgba32_to_yuv_8bpp(
    const __m256i rgba_in[static 4],
    const __m256i *const coefficients,
    const __m256i *const pairwise_coefficients,
    const uint8_t shr
) {
    return m256i_concat_i32_to_i8(
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[0], coefficients, pairwise_coefficients, shr),
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[1], coefficients, pairwise_coefficients, shr),
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[2], coefficients, pairwise_coefficients, shr),
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[3], coefficients, pairwise_coefficients, shr)
    );
}
static inline __m256i SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
convert_32px_rgba32_to_yuv_8bpp__laneswapped(
    const __m256i rgba_in[static 4],
    const __m256i *const coefficients,
    const __m256i *const pairwise_coefficients,
    const uint8_t shr
) {
    return m256i_concat_i32_to_i8__laneswapped(
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[0], coefficients, pairwise_coefficients, shr),
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[1], coefficients, pairwise_coefficients, shr),
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[2], coefficients, pairwise_coefficients, shr),
        convert_rgba32_to_yuv_y_32bpp(&rgba_in[3], coefficients, pairwise_coefficients, shr)
    );
}


static void SCRANROT_TARGET_AVX2
transform_framebuffer_to_yuv420__avx2_unaligned__rotate_270(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 64 && KERNEL_TILE_HEIGHT_PX == 64, "270 kernel assumes 64x64 RGBA32 tiles.");

    const __m256i rgba32_shuffle_mask_256 = rgba32_shuffle_to_m256i(
        rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
    );

    const __m256i y_coefficients          = get_yuv_y_rbga_coefficients_256();
    const __m256i u_coefficients          = get_yuv_u_rbga_coefficients_256();
    const __m256i v_coefficients          = get_yuv_v_rbga_coefficients_256();
    const __m256i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients_256();
    const __m256i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients_256();
    const __m256i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients_256();

    const int dst_height_px = src_width_px;
    uint8_t *dst_y_start = scranrot_yuv420_y_last_row_start( y_plane, dst_height_px, y_stride);
    uint8_t *dst_u_start = scranrot_yuv420_uv_last_row_start(u_plane, dst_height_px, u_stride);
    uint8_t *dst_v_start = scranrot_yuv420_uv_last_row_start(v_plane, dst_height_px, v_stride);


    for (int y = 0; y <= src_height_px - 64; y += 64) {
        for (int x = 0; x <= src_width_px - 64; x += 64) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            __m256i u_i8_4bpp_final[32];
            __m256i v_i8_4bpp_final[32];

            for (int _y = 0; _y < 64; _y += 32) {

                // NOTE: Storing row pairs for u/v in inner loop, and unpackhi/lo into full rows, in
                //       order to only need to sit on 8 of these at a time (since righthand sub-tile
                //       will be combining into _final already), seems to give worse performance.
                __m256i u_i16_8bpp_xyavg[16][2];
                __m256i v_i16_8bpp_xyavg[16][2];

                for (int _x = 0; _x < 64; _x += 32) {

                    const uint8_t _xi = _x >> 5; // divide by 32
                    __m256i y_8bpp_final[32];
                    const uint8_t *const src_subtile = src + (y+_y)*src_stride_bytes + (x+_x)*RGBA32_PIXEL_STRIDE;

                    for (int j = 0; j < 32; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m256i rgba_32bpp[2][4] = { // 4 YMM registers hold one 32px RGBA32 row
                            {
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                            }, {
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                            },
                        };

                        y_8bpp_final[j+0] = convert_32px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8);
                        y_8bpp_final[j+1] = convert_32px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m256i rgba_32bpp_rows_avg[4] = {
                            _mm256_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm256_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm256_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm256_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        // U
                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &u_pairwise_coefficients, 8
                                                        );
                        // V
                        v_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &v_pairwise_coefficients, 8
                                                        );
                    }


                    // Store Y (Inner tile)
                    transpose_inplace_32x32_8bpp(y_8bpp_final);
                    {
                        uint8_t *dst_y = dst_y_start + (y+_y) - (x+_x)*y_stride;
                        for (int j = 0; j < 32; ++j) {
                            storeu_m256i(dst_y, y_8bpp_final[j]);
                            dst_y -= y_stride;
                        }
                    }

                }

                // Finalize U,V for entire outer tile row
                for (int k = 0; k < 16; ++k) {
                    u_i8_4bpp_final[(_y/2)+k] = m256i_concat_i16_to_i8(
                                                     u_i16_8bpp_xyavg[k][0],
                                                     u_i16_8bpp_xyavg[k][1]
                                                );
                    v_i8_4bpp_final[(_y/2)+k] = m256i_concat_i16_to_i8(
                                                     v_i16_8bpp_xyavg[k][0],
                                                     v_i16_8bpp_xyavg[k][1]
                                                );
                }

            }

            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));

            // Store U
            transpose_inplace_32x32_8bpp(u_i8_4bpp_final);
            {
                uint8_t *dst_u = dst_u_start + (y/2) - (x/2)*u_stride;
                for (int j = 0; j < 32; ++j) {
                    storeu_m256i(dst_u, u_i8_4bpp_final[j]);
                    dst_u -= u_stride;
                }
            }

            // Store V
            transpose_inplace_32x32_8bpp(v_i8_4bpp_final);
            {
                uint8_t *dst_v = dst_v_start + (y/2) - (x/2)*v_stride;
                for (int j = 0; j < 32; ++j) {
                    storeu_m256i(dst_v, v_i8_4bpp_final[j]);
                    dst_v -= v_stride;
                }
            }
        }
    }
}

static void SCRANROT_TARGET_AVX2
transform_framebuffer_to_yuv420__avx2_unaligned__rotate_180(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 64 && KERNEL_TILE_HEIGHT_PX >= 2, "180 kernel uses 2x64 RGBA32 tiles.");

    const __m256i rgba32_shuffle_mask_256 = rotate_180_get_modified_rgba_shuffle_256(
        rgba32_shuffle_to_m256i(
            rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
        )
    );

    const __m256i y_coefficients          = get_yuv_y_rbga_coefficients_256();
    const __m256i u_coefficients          = get_yuv_u_rbga_coefficients_256();
    const __m256i v_coefficients          = get_yuv_v_rbga_coefficients_256();
    const __m256i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients_256();
    const __m256i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients_256();
    const __m256i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients_256();


    uint8_t *dst_y_start = scranrot_yuv420_y_last_row_end( y_plane, src_width_px, src_height_px, y_stride) - sizeof(__m256i);
    uint8_t *dst_u_start = scranrot_yuv420_uv_last_row_end(u_plane, src_width_px, src_height_px, u_stride) - sizeof(__m256i);
    uint8_t *dst_v_start = scranrot_yuv420_uv_last_row_end(v_plane, src_width_px, src_height_px, v_stride) - sizeof(__m256i);


    for (int y = 0; y < src_height_px; y += 2) {
        uint8_t const *_src   = src         + (y * src_stride_bytes);
        uint8_t       *_dst_y = dst_y_start - (y * y_stride);

        for (int x = 0; x < src_width_px; x += 64) {
            __m256i u_i16_8bpp_xyavg[2];
            __m256i v_i16_8bpp_xyavg[2];

            for (int _x = 0; _x < 64; _x += 32) {

                // const int _xi = _x >> 5; // Divide by 32
                const int _xi_reversed = 1 - (_x >> 5);

                const __m256i rgba_32bpp[2][4] = {
                    {
                        _mm256_shuffle_epi8(loadu_m256i(_src                    + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src                    + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src                    +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src                    +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                    }, {
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                    },
                };

                // Store Y
                storeu_m256i(_dst_y           , convert_32px_rgba32_to_yuv_8bpp__laneswapped(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8));
                storeu_m256i(_dst_y - y_stride, convert_32px_rgba32_to_yuv_8bpp__laneswapped(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8));

                // Store intermediate U,V
                // We average the two rows before converting, to reduce required calculation
                {
                    const __m256i rgba_32bpp_rows_avg[4] = {
                        _mm256_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                        _mm256_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                        _mm256_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                        _mm256_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                    };
                    u_i16_8bpp_xyavg[_xi_reversed] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp__laneswapped(rgba_32bpp_rows_avg, &u_coefficients, &u_pairwise_coefficients, 8);
                    v_i16_8bpp_xyavg[_xi_reversed] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp__laneswapped(rgba_32bpp_rows_avg, &v_coefficients, &v_pairwise_coefficients, 8);
                }

                _Static_assert(32 == sizeof(__m256i), "");
                _src   += 32 * RGBA32_PIXEL_STRIDE;
                _dst_y -= 32;
            }

            // (the u/v coordinates if we had 0 rotation)
            int x_uv = x/2;
            int y_uv = y/2;

            uint8_t *_dst_u = dst_u_start - (y_uv * u_stride) - (x_uv);
            storeu_m256i(
                _dst_u,
                m256i_concat_i16_to_i8(
                    u_i16_8bpp_xyavg[0],
                    u_i16_8bpp_xyavg[1]
                )
            );

            uint8_t *_dst_v = dst_v_start - (y_uv * v_stride) - (x_uv);
            storeu_m256i(
                _dst_v,
                m256i_concat_i16_to_i8(
                    v_i16_8bpp_xyavg[0],
                    v_i16_8bpp_xyavg[1]
                )
            );
        }
    }
}

static void SCRANROT_TARGET_AVX2
transform_framebuffer_to_yuv420__avx2_unaligned__rotate_90(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 64 && KERNEL_TILE_HEIGHT_PX == 64, "90 kernel assumes 64x64 RGBA32 tiles.");

    const __m256i rgba32_shuffle_mask_256 = rgba32_shuffle_to_m256i(
        rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
    );

    const __m256i y_coefficients          = get_yuv_y_rbga_coefficients_256();
    const __m256i u_coefficients          = get_yuv_u_rbga_coefficients_256();
    const __m256i v_coefficients          = get_yuv_v_rbga_coefficients_256();
    const __m256i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients_256();
    const __m256i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients_256();
    const __m256i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients_256();

    const int dst_width_px = src_height_px;
    uint8_t *dst_y_start = scranrot_yuv420_y_row_end( y_plane, dst_width_px) - sizeof(__m256i);
    uint8_t *dst_u_start = scranrot_yuv420_uv_row_end(u_plane, dst_width_px) - sizeof(__m256i);
    uint8_t *dst_v_start = scranrot_yuv420_uv_row_end(v_plane, dst_width_px) - sizeof(__m256i);

    for (int y = 0; y <= src_height_px - 64; y += 64) {
        for (int x = 0; x <= src_width_px - 64; x += 64) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            __m256i u_i8_4bpp_final[32];
            __m256i v_i8_4bpp_final[32];

            for (int _y = 0; _y < 64; _y += 32) {

                // NOTE: Storing row pairs for u/v in inner loop, and unpackhi/lo into full rows, in
                //       order to only need to sit on 8 of these at a time (since righthand sub-tile
                //       will be combining into _final already), seems to give worse performance.
                __m256i u_i16_8bpp_xyavg[16][2];
                __m256i v_i16_8bpp_xyavg[16][2];

                for (int _x = 0; _x < 64; _x += 32) {

                    const uint8_t _xi = _x >> 5; // divide by 32
                    __m256i y_8bpp_final[32];
                    const uint8_t *const src_subtile = src + (y+_y)*src_stride_bytes + (x+_x)*RGBA32_PIXEL_STRIDE;

                    for (int j = 0; j < 32; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m256i rgba_32bpp[2][4] = { // 4 YMM registers hold one 32px RGBA32 row
                            {
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+0)*src_stride_bytes + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                            }, {
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                                _mm256_shuffle_epi8(loadu_m256i(src_subtile + (j+1)*src_stride_bytes + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                            },
                        };

                        y_8bpp_final[j+0] = convert_32px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8);
                        y_8bpp_final[j+1] = convert_32px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m256i rgba_32bpp_rows_avg[4] = {
                            _mm256_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm256_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm256_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm256_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        // U
                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &u_pairwise_coefficients, 8
                                                        );
                        // V
                        v_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &v_pairwise_coefficients, 8
                                                        );
                    }


                    // Store Y (Inner tile)
                    rotate90_inplace_32x32_8bpp(y_8bpp_final);
                    {
                        uint8_t *dst_y = dst_y_start - (y+_y) + (x+_x)*y_stride;
                        for (int j = 0; j < 32; ++j) {
                            storeu_m256i(dst_y, y_8bpp_final[j]);
                            dst_y += y_stride;
                        }
                    }

                }


                // Finalize U,V for entire outer tile row
                for (int k = 0; k < 16; ++k) {
                    u_i8_4bpp_final[(_y/2)+k] = m256i_concat_i16_to_i8(
                                                     u_i16_8bpp_xyavg[k][0],
                                                     u_i16_8bpp_xyavg[k][1]
                                                );
                    v_i8_4bpp_final[(_y/2)+k] = m256i_concat_i16_to_i8(
                                                     v_i16_8bpp_xyavg[k][0],
                                                     v_i16_8bpp_xyavg[k][1]
                                                );
                }
            }


            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));

            // Store U
            rotate90_inplace_32x32_8bpp(u_i8_4bpp_final);
            {
                uint8_t *dst_u = dst_u_start - (y/2) + (x/2)*u_stride;
                for (int j = 0; j < 32; ++j) {
                    storeu_m256i(dst_u, u_i8_4bpp_final[j]);
                    dst_u += u_stride;
                }
            }

            // Store V
            rotate90_inplace_32x32_8bpp(v_i8_4bpp_final);
            {
                uint8_t *dst_v = dst_v_start - (y/2) + (x/2)*v_stride;
                for (int j = 0; j < 32; ++j) {
                    storeu_m256i(dst_v, v_i8_4bpp_final[j]);
                    dst_v += v_stride;
                }
            }

        }
    }
}

static void SCRANROT_TARGET_AVX2
transform_framebuffer_to_yuv420__avx2_unaligned__rotate_0(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    _Static_assert(KERNEL_TILE_WIDTH_PX == 64 && KERNEL_TILE_HEIGHT_PX >= 2, "0 kernel uses 2x64 RGBA32 tiles.");

    const __m256i rgba32_shuffle_mask_256 = rgba32_shuffle_to_m256i(
        rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
    );

    const __m256i y_coefficients          = get_yuv_y_rbga_coefficients_256();
    const __m256i u_coefficients          = get_yuv_u_rbga_coefficients_256();
    const __m256i v_coefficients          = get_yuv_v_rbga_coefficients_256();
    const __m256i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients_256();
    const __m256i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients_256();
    const __m256i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients_256();

    for (int y = 0; y < src_height_px; y += 2) {
        uint8_t const *_src   = src     + (y * src_stride_bytes);
        uint8_t       *_dst_y = y_plane + (y * y_stride);

        for (int x = 0; x < src_width_px; x += 64) {
            __m256i u_i16_8bpp_xyavg[2];
            __m256i v_i16_8bpp_xyavg[2];

            for (int _x = 0; _x < 64; _x += 32) {

                const int _xi = _x >> 5; // Divide by 32

                const __m256i rgba_32bpp[2][4] = {
                    {
                        _mm256_shuffle_epi8(loadu_m256i(_src +                     0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src +                     8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src +                    16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src +                    24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                    }, {
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes +  0*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes +  8*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes + 16*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                        _mm256_shuffle_epi8(loadu_m256i(_src + src_stride_bytes + 24*RGBA32_PIXEL_STRIDE), rgba32_shuffle_mask_256),
                    },
                };

                // Store Y
                storeu_m256i(_dst_y           , convert_32px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8));
                storeu_m256i(_dst_y + y_stride, convert_32px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8));

                // Store intermediate U,V
                // We average the two rows before converting, to reduce required calculation
                {
                    const __m256i rgba_32bpp_rows_avg[4] = {
                        _mm256_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                        _mm256_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                        _mm256_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                        _mm256_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                    };
                    u_i16_8bpp_xyavg[_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(rgba_32bpp_rows_avg, &u_coefficients, &u_pairwise_coefficients, 8);
                    v_i16_8bpp_xyavg[_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(rgba_32bpp_rows_avg, &v_coefficients, &v_pairwise_coefficients, 8);
                }

                _Static_assert(32 == sizeof(__m256i), "");
                _src   += 32 * RGBA32_PIXEL_STRIDE;
                _dst_y += 32;
            }

            int x_uv = x/2;
            int y_uv = y/2;

            uint8_t *_dst_u = u_plane + (y_uv * u_stride) + (x_uv);
            storeu_m256i(
                _dst_u,
                m256i_concat_i16_to_i8(
                    u_i16_8bpp_xyavg[0],
                    u_i16_8bpp_xyavg[1]
                )
            );

            uint8_t *_dst_v = v_plane + (y_uv * v_stride) + (x_uv);
            storeu_m256i(
                _dst_v,
                m256i_concat_i16_to_i8(
                    v_i16_8bpp_xyavg[0],
                    v_i16_8bpp_xyavg[1]
                )
            );
        }
    }
}


bool
scranrot_transform_framebuffer_to_yuv420_avx2(
    const uint8_t *restrict src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    uint8_t *restrict dst,
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    // OUT:
    uint8_t **dst_y, int *dst_y_stride,
    uint8_t **dst_u, int *dst_u_stride,
    uint8_t **dst_v, int *dst_v_stride
) {
    if (src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX) {
        return scranrot_transform_framebuffer_to_yuv420_ssse3(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst, rgba_shuffle_mask, transform,
            dst_y, dst_y_stride,
            dst_u, dst_u_stride,
            dst_v, dst_v_stride
        );
    }

    scranrot_transform_framebuffer_to_yuv_impl_fn transform_fn = NULL;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_to_yuv420__avx2_unaligned__rotate_270; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv420__avx2_unaligned__rotate_180; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv420__avx2_unaligned__rotate_90 ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv420__avx2_unaligned__rotate_0  ; break;
    default:
        // XXX TODO: Implement flipped
        return scranrot_transform_framebuffer_to_yuv420_fallback(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst, rgba_shuffle_mask, transform,
            dst_y, dst_y_stride,
            dst_u, dst_u_stride,
            dst_v, dst_v_stride
        );
    }

    SCRANROT_ASSERT(transform_fn != NULL);
    return transform_framebuffer_to_yuv420__generic_dispatcher(
        src, src_width_px, src_height_px, src_stride_bytes,
        dst,

        transform_fn,
        transform, rgba_shuffle_mask,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}


#endif
