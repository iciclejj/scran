#if defined(__x86_64__) || defined(__i386__)


#include <stdbool.h>
#include <stdint.h>
#include <tmmintrin.h>

#include "../include/scranrot.h"
#include "./sse2.h"
#include "./generic.h"


enum {
    RGBA32_PIXELS_PER_XMM = 4,

    KERNEL_TILE_WIDTH_PX  = 32,
    KERNEL_TILE_HEIGHT_PX = 32,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};
_Static_assert(RGBA32_PIXELS_PER_XMM * RGBA32_PIXEL_STRIDE == sizeof(__m128i), "This file assumes an XMM register holds 4 RGBA32 pixels.");


// Y coefficients are split across a and b coefficient arrays, since we cannot
// fit them as 8bit signed ints. We use the sum of multiplying with each array
// to get the correct result.
// Target: 77,150,29
//
// NOTE: We also keep the combined values as low as possible within these
// constraints, so that it also won't overflow the 16-bit ints after
// multiplication.
//   I.e. this:        77, 23,29,0  0,127,0,0
//   Instead of this:  77,127,29,0  0, 23,0,0
static inline __m128i
_get_yuv_y_coefficients_a() {
    return _mm_setr_epi8(77,23,29,0,  77,23,29,0,  77,23,29,0,  77,23,29,0);
}
static inline __m128i
_get_yuv_y_coefficients_b() {
    return _mm_setr_epi8(0,127,0,0,   0,127,0,0,   0,127,0,0,   0,127,0,0);
}

static inline __m128i
_get_yuv_u_coefficients() {
    return _mm_setr_epi8(-43,-84,127,0, -43,-84,127,0, -43,-84,127,0, -43,-84,127,0);
}
static inline __m128i
_get_yuv_v_coefficients() {
    return _mm_setr_epi8(127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0);
}


// TODO: Use unrolled loops for all the transpose and rotation functions.

// Transpose is equivalent to 270 degree rotation + flip.
//   We can simply read the result in reverse order in order to get the 270.
//   Reading in reverse order is often simpler for rotating the entire image
//   anyways, depending on how we do the pointer walks. At least it is simpler
//   inside of our loops here.
//
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_16x16_8bpp_transpose_inplace(
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

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline void
_16x16_8bpp_rotate_90_inplace(
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

// __attribute__((optimize("unroll-loops")))
// SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
// static inline __m128i
// _rgba32_to_yuv_plane_32bpp_unsigned_coefficients(
//     const __m128i *const rgba_in,
//     const __m128i *const coefficients,
//     // Should probably always be 1. Required as an arg to not re-initialize every time.
//     const __m128i *const hadamard_scaler,
//     const uint8_t shr
// ) {
//     return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
//               _mm_madd_epi16( // Y32 := [_Y32=(r*cr+g*cg+b*cb+a*ca), ...] => Y32 == [y32<<shr, ...]
//                   _mm_maddubs_epi16(*rgba_in, *coefficients),
//                   *hadamard_scaler
//               ),
//               shr
//           );
// }

// Same as non-x2 function, but takes two coefficient arrays. Intended to be
// used when the coefficient values don't fit within signed 8-bit.
// Coefficient arrays a,b are treated as one array a+b (element-wise sum)
__attribute__((optimize("unroll-loops")))
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2(
    const __m128i *const rgba_in,
    const __m128i *const coefficients_a,
    const __m128i *const coefficients_b,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i *const hadamard_scaler,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const uint8_t shr
) {
    return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
              _mm_add_epi32(
                  _mm_madd_epi16( // Y32 := [_Y32=(r*cr+g*cg+b*cb+a*ca), ...] => Y32 == [y32<<shr, ...]
                      _mm_maddubs_epi16(*rgba_in, *coefficients_a),
                      *hadamard_scaler
                  ),
                  _mm_madd_epi16(
                      _mm_maddubs_epi16(*rgba_in, *coefficients_b),
                      *hadamard_scaler
                  )
              ),
              shr
          );
}

__attribute__((optimize("unroll-loops")))
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
_rgba32_to_yuv_plane_32bpp_signed_coefficients(
    const __m128i *const rgba_in,
    const __m128i *const coefficients,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i *const hadamard_scaler,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const uint8_t shr
) {
    // We need to represent our values as signed, so we normalize them by adding
    // the max signed absolute value, to take the (post-shr) range from -127:128 -> 0:255
    // TODO: Can we alter our coefficients instead?
    const __m128i uv_s_to_us_offset_epi32 = _mm_set1_epi32((128 << 8) + 128);

    return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
               _mm_add_epi32( // Y32 := uv_s_to_us_offset(Y32)
                   _mm_madd_epi16( // Y32 := [_Y32=(r*cr+g*cg+b*cb+a*ca), ...] => Y32 == [(+/-)y32<<shr, ...]
                       _mm_maddubs_epi16(*rgba_in, *coefficients),
                       *hadamard_scaler
                   ),
                   uv_s_to_us_offset_epi32
               ),
               shr
          );
}

// SSSE3 replacement for SSE4.1's _mm_packus_epi32.
//
// Safe to use as a replacement as long as the input values fall within [0, INT16_MAX]
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
_ssse3_packus_epi32_assume_0_to_i16max(__m128i a, __m128i b) {
    return _mm_packs_epi32(a, b);
}

__attribute__((optimize("unroll-loops")))
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
    const __m128i rgba_in[4],
    const __m128i *const coefficients,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i *const hadamard_scaler,
    const uint8_t shr
) {
    // Returns: [ (i16)(a0+a1)/2, (i16)(a2+a3)/2, ...]

    const __m128i hadamard_identity = _mm_set1_epi16(1);

    return _mm_srai_epi16( // V16_avg([y,x])
               _ssse3_packus_epi32_assume_0_to_i16max( // V16_avg(y+x)
                   _mm_madd_epi16 ( // V32_avg(y+x)
                       _ssse3_packus_epi32_assume_0_to_i16max( // V16_avg(y)
                           _rgba32_to_yuv_plane_32bpp_signed_coefficients( // V32_yavg
                               &rgba_in[0], coefficients, hadamard_scaler, shr
                           ),
                           _rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[1], coefficients, hadamard_scaler, shr
                           )
                       ),
                       hadamard_identity // NOTE: This one must be identity (all 1). Don't use the passed hadamard_scaler.
                   ),

                   _mm_madd_epi16 (
                       _ssse3_packus_epi32_assume_0_to_i16max(
                           _rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[2], coefficients, hadamard_scaler, shr
                           ),
                           _rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[3], coefficients, hadamard_scaler, shr
                           )
                       ),
                       hadamard_identity
                   )
               ),
               1 // Divide by 2 to get averages of the madds
           );
}

__attribute__((optimize("unroll-loops")))
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
_16px_rgba32_to_yuv_8bpp(
    const __m128i rgba_in[4],
    const __m128i *const coefficients_a,
    const __m128i *const coefficients_b,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i *const hadamard_scaler,
    const uint8_t shr
) {
    // TODO: Function to get the intermediate A32 value

    return _mm_packus_epi16( // Y8 := [Y16 & 0xFF, Y16_1 & 0xFF] => Y8 == [y8, ...]
              _ssse3_packus_epi32_assume_0_to_i16max( // Y16 := [Y32 & 0xFFFF, Y32_1 & 0xFFFF] => Y16 == [y16, ...]
                  _rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32 == [y32, ...]
                      &rgba_in[0], coefficients_a, coefficients_b, hadamard_scaler, shr
                  ),
                  _rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_1
                      &rgba_in[1], coefficients_a, coefficients_b, hadamard_scaler, shr
                  )
              ),

              _ssse3_packus_epi32_assume_0_to_i16max( // A16_1
                  _rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_2
                      &rgba_in[2], coefficients_a, coefficients_b, hadamard_scaler, shr
                  ),
                  _rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_3
                      &rgba_in[3], coefficients_a, coefficients_b, hadamard_scaler, shr
                  )
              )
          );
}

SCRANROT_TARGET_SSSE3
static void
transform_framebuffer_to_yuv__ssse3_unaligned__rotate_270(
    const void *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    // TODO: Rename to be consistent with dst_y_stride naming scheme (or rename elsewhere)
    uint8_t *restrict y_line, int y_linesize,
    uint8_t *restrict u_line, int u_linesize,
    uint8_t *restrict v_line, int v_linesize,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    const __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "270 kernel assumes 32x32 RGBA32 tiles.");

    const int dst_height_px = src_width_px;

    const __m128i y_coefficients_a = _get_yuv_y_coefficients_a();
    const __m128i y_coefficients_b = _get_yuv_y_coefficients_b();
    const __m128i u_coefficients   = _get_yuv_u_coefficients();
    const __m128i v_coefficients   = _get_yuv_v_coefficients();

    // TODO: Better to just _mm_set1_epi16(1) in each location?
    const __m128i hadam_ident_epi16 = _mm_set1_epi16(1);


    for (int y = 0; y <= src_height_px - 32; y += 32) {
        for (int x = 0; x <= src_width_px - 32; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            // TODO: Probably store row pairs for u/v in inner loop, and
            //       unpackhi/lo into full rows, to reduce cache usage.
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const uint8_t _xi = _x >> 4; // divide by 16
                    __m128i y_8bpp_final[16];

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            }, {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            },
                        };

                        y_8bpp_final[j+0] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);
                        y_8bpp_final[j+1] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        // U
                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &hadam_ident_epi16, 8
                                                        );
                        // V
                        v_i16_8bpp_xyavg[j_yavg][_xi] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &hadam_ident_epi16, 8
                                                        );
                    }


                    // Store Y
                    //   (Inner tile)
                    _16x16_8bpp_transpose_inplace(y_8bpp_final);
                    for (int j = 0; j < 16; ++j) {
                        _mm_storeu_si128((__m128i*)(y_line + (y+_y) + ((dst_height_px-1-(j))-(x+_x))*y_linesize), y_8bpp_final[j]);
                    }
                }

                // Finalize U,V for entire outer tile row
                for (int k = 0; k < 8; ++k) {
                    u_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     u_i16_8bpp_xyavg[k][0],
                                                     u_i16_8bpp_xyavg[k][1]
                                                );
                    v_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     v_i16_8bpp_xyavg[k][0],
                                                     v_i16_8bpp_xyavg[k][1]
                                                );
                }

            }

            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));

            // Store U
            _16x16_8bpp_transpose_inplace(u_i8_4bpp_final);
            for (int j = 0; j < 16; ++j) {
                _mm_storeu_si128((__m128i*)(u_line + (y/2) + ( ((dst_height_px-x)/2)-1-(j))*u_linesize), u_i8_4bpp_final[j]);
            }

            // Store V
            _16x16_8bpp_transpose_inplace(v_i8_4bpp_final);
            for (int j = 0; j < 16; ++j) {
                _mm_storeu_si128((__m128i*)(v_line + (y/2) + ( ((dst_height_px-x)/2)-1-(j))*v_linesize), v_i8_4bpp_final[j]);
            }
        }
    }
}

SCRANROT_TARGET_SSSE3
static void
transform_framebuffer_to_yuv__ssse3_unaligned__rotate_180(
    const void *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    // TODO: Rename to be consistent with dst_y_stride naming scheme (or rename elsewhere)
    uint8_t *restrict y_line, int y_linesize,
    uint8_t *restrict u_line, int u_linesize,
    uint8_t *restrict v_line, int v_linesize,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    const __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rotate_180_get_modified_rgba_shuffle(
        *(__m128i *)_rgba32_shuffle_mask_128
    );

    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "180 kernel assumes 32x32 RGBA32 tiles.");

    const __m128i y_coefficients_a = _get_yuv_y_coefficients_a();
    const __m128i y_coefficients_b = _get_yuv_y_coefficients_b();
    const __m128i u_coefficients   = _get_yuv_u_coefficients();
    const __m128i v_coefficients   = _get_yuv_v_coefficients();

    // TODO: Better to just _mm_set1_epi16(1) in each location?
    const __m128i hadam_ident_epi16 = _mm_set1_epi16(1);

    // TODO: Do this sizeof(__m128i) pre-calculation thing elsewhere too, instead of -16 everywhere?
    uint8_t *y_line_ = y_line + (src_height_px-1)   * y_linesize + (src_width_px   - sizeof(__m128i));
    uint8_t *u_line_ = u_line + (src_height_px-1)/2 * u_linesize + (src_width_px/2 - sizeof(__m128i));
    uint8_t *v_line_ = v_line + (src_height_px-1)/2 * v_linesize + (src_width_px/2 - sizeof(__m128i));


    for (int y = 0; y < src_height_px; y += 32) {
        for (int x = 0; x < src_width_px; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            // TODO: Probably store row pairs for u/v in inner loop, and
            //       unpackhi/lo into full rows, to reduce cache usage.
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    // const uint8_t _xi          = _x >> 4; // divide by 16
                    // NOTE: 180 uses reversed _x index order here compared to the other rotations
                    const uint8_t _xi_reversed = (16 - _x) >> 4;
                    __m128i y_8bpp_final[16];

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        // NOTE: 180 uses reversed load order here compared to the other rotations
                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            }, {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            },
                        };

                        y_8bpp_final[j+0] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);
                        y_8bpp_final[j+1] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        // U
                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi_reversed] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &hadam_ident_epi16, 8
                                                        );
                        // V
                        v_i16_8bpp_xyavg[j_yavg][_xi_reversed] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &hadam_ident_epi16, 8
                                                        );
                    }

                    // Store Y
                    for (int j = 0; j < 16; ++j) {
                        _mm_storeu_si128((__m128i*)(y_line_ - ((y+_y+j)*y_linesize) - (x+_x)), y_8bpp_final[j]);
                    }
                }

                // Finalize U,V for entire outer tile row
                for (int k = 0; k < 8; ++k) {
                    u_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     u_i16_8bpp_xyavg[k][0],
                                                     u_i16_8bpp_xyavg[k][1]
                                                );
                    v_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     v_i16_8bpp_xyavg[k][0],
                                                     v_i16_8bpp_xyavg[k][1]
                                                );
                }

            }

            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));
            const int uv_y = y/2;
            const int uv_x = x/2;

            // Store U,V
            for (int l = 0; l < 16; ++l) {
                _mm_storeu_si128((__m128i*)(u_line_ - ((uv_y+l)*u_linesize) - (uv_x)), u_i8_4bpp_final[l]);
                _mm_storeu_si128((__m128i*)(v_line_ - ((uv_y+l)*v_linesize) - (uv_x)), v_i8_4bpp_final[l]);
            }

        }
    }
}

SCRANROT_TARGET_SSSE3
static void
transform_framebuffer_to_yuv__ssse3_unaligned__rotate_90(
    const void *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    // TODO: Rename to be consistent with dst_y_stride naming scheme (or rename elsewhere)
    uint8_t *restrict y_line, int y_linesize,
    uint8_t *restrict u_line, int u_linesize,
    uint8_t *restrict v_line, int v_linesize,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    const __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "90 kernel assumes 32x32 RGBA32 tiles.");

    const int dst_width_px = src_height_px;

    const __m128i y_coefficients_a = _get_yuv_y_coefficients_a();
    const __m128i y_coefficients_b = _get_yuv_y_coefficients_b();
    const __m128i u_coefficients   = _get_yuv_u_coefficients();
    const __m128i v_coefficients   = _get_yuv_v_coefficients();

    // TODO: Better to just _mm_set1_epi16(1) in each location?
    const __m128i hadam_ident_epi16 = _mm_set1_epi16(1);


    for (int y = 0; y <= src_height_px - 32; y += 32) {
        for (int x = 0; x <= src_width_px - 32; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            // TODO: Probably store row pairs for u/v in inner loop, and
            //       unpackhi/lo into full rows, to reduce cache usage.
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const uint8_t _xi = _x >> 4; // divide by 16
                    __m128i y_8bpp_final[16];

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            }, {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            },
                        };


                        y_8bpp_final[j+0] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);
                        y_8bpp_final[j+1] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        // U
                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &hadam_ident_epi16, 8
                                                        );
                        // V
                        v_i16_8bpp_xyavg[j_yavg][_xi] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &hadam_ident_epi16, 8
                                                        );
                    }


                    // Store Y
                    //   (Inner tile)
                    _16x16_8bpp_rotate_90_inplace(y_8bpp_final);
                    for (int j = 0; j < 16; ++j) {
                        // 90-rotation dst starts at the right edge and moves backwards, so the current tile
                        // is always positioined "behind" us.
                        static const int dst_col_offset = -16;
                        _mm_storeu_si128((__m128i*)(y_line + (dst_width_px + dst_col_offset - (y+_y)) + (x+_x+j)*y_linesize), y_8bpp_final[j]);
                    }
                }

                // Finalize U,V for entire outer tile row
                for (int k = 0; k < 8; ++k) {
                    u_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     u_i16_8bpp_xyavg[k][0],
                                                     u_i16_8bpp_xyavg[k][1]
                                                );
                    v_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     v_i16_8bpp_xyavg[k][0],
                                                     v_i16_8bpp_xyavg[k][1]
                                                );
                }

            }

            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));

            // Store U
            _16x16_8bpp_rotate_90_inplace(u_i8_4bpp_final);
            for (int j = 0; j < 16; ++j) {
                static const int dst_col_offset = -16; // See comment at first usage
                _mm_storeu_si128((__m128i*)(u_line + (dst_width_px/2 - y/2 + dst_col_offset) + (x/2+j)*u_linesize), u_i8_4bpp_final[j]);
            }

            // Store V
            _16x16_8bpp_rotate_90_inplace(v_i8_4bpp_final);
            for (int j = 0; j < 16; ++j) {
                static const int dst_col_offset = -16; // See comment at first usage
                _mm_storeu_si128((__m128i*)(v_line + (dst_width_px/2 - y/2 + dst_col_offset) + (x/2+j)*v_linesize), v_i8_4bpp_final[j]);
            }
        }
    }
}

SCRANROT_TARGET_SSSE3
static void
transform_framebuffer_to_yuv__ssse3_unaligned__rotate_0(
    const void *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    // TODO: Rename to be consistent with dst_y_stride naming scheme (or rename elsewhere)
    uint8_t *restrict y_line, int y_linesize,
    uint8_t *restrict u_line, int u_linesize,
    uint8_t *restrict v_line, int v_linesize,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    const __m128i rgba32_shuffle_mask_128 = *(__m128i *)_rgba32_shuffle_mask_128;

    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "0 kernel assumes 32x32 RGBA32 tiles.");

    const __m128i y_coefficients_a = _get_yuv_y_coefficients_a();
    const __m128i y_coefficients_b = _get_yuv_y_coefficients_b();
    const __m128i u_coefficients   = _get_yuv_u_coefficients();
    const __m128i v_coefficients   = _get_yuv_v_coefficients();

    // TODO: Better to just _mm_set1_epi16(1) in each location?
    const __m128i hadam_ident_epi16 = _mm_set1_epi16(1);


    for (int y = 0; y < src_height_px; y += 32) {
        for (int x = 0; x < src_width_px; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            // TODO: Probably store row pairs for u/v in inner loop, and
            //       unpackhi/lo into full rows, to reduce cache usage.
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const uint8_t _xi = _x >> 4; // divide by 16
                    __m128i y_8bpp_final[16];

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+0) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            },
                            {
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  0) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  4) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x +  8) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                                _mm_shuffle_epi8(
                                    _mm_loadu_si128(src + ((y + _y + j+1) * src_stride_bytes) + (x + _x + 12) * RGBA32_PIXEL_STRIDE),
                                    rgba32_shuffle_mask_128
                                ),
                            },
                        };

                        y_8bpp_final[j+0] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);
                        y_8bpp_final[j+1] = _16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients_a, &y_coefficients_b, &hadam_ident_epi16, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &hadam_ident_epi16, 8
                                                        );
                        v_i16_8bpp_xyavg[j_yavg][_xi] = _16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &hadam_ident_epi16, 8
                                                        );
                    }

                    // Store YUV:  Y
                    for (int j = 0; j < 16; ++j) {
                        _mm_storeu_si128((__m128i*)(y_line + (y+_y+j)*y_linesize + x+_x), y_8bpp_final[j]);
                    }
                }

                // Finalize uv for entire outer tile row
                for (int k = 0; k < 8; ++k) {
                    u_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     u_i16_8bpp_xyavg[k][0],
                                                     u_i16_8bpp_xyavg[k][1]
                                                );
                    v_i8_4bpp_final[(_y/2)+k] = _mm_packus_epi16(
                                                     v_i16_8bpp_xyavg[k][0],
                                                     v_i16_8bpp_xyavg[k][1]
                                                );
                }

            }

            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));
            const int uv_y = y/2;
            const int uv_x = x/2;

            // Store U,V
            for (int l = 0; l < 16; ++l) {
                _mm_storeu_si128((__m128i*)(u_line + (uv_y+l)*u_linesize + uv_x), u_i8_4bpp_final[l]);
                _mm_storeu_si128((__m128i*)(v_line + (uv_y+l)*v_linesize + uv_x), v_i8_4bpp_final[l]);
            }

        }
    }
}


bool
scranrot_transform_framebuffer_to_yuv420_ssse3__unaligned(
    const void *src,
    int src_width_px,
    int src_height_px,
    int src_stride_bytes,
    void *dst,
    uint32_t rgba_shuffle_mask,
    enum scranrot_transform transform,
    // OUT:
    uint8_t **dst_y, int *dst_y_stride,
    uint8_t **dst_u, int *dst_u_stride,
    uint8_t **dst_v, int *dst_v_stride
) {
    if (src_width_px < MIN_TILE_WIDTH_PX || src_height_px < MIN_TILE_HEIGHT_PX) {
        return scranrot_transform_framebuffer_to_yuv420_fallback(
            src, src_width_px, src_height_px, src_stride_bytes,
            dst, rgba_shuffle_mask, transform,
            dst_y, dst_y_stride,
            dst_u, dst_u_stride,
            dst_v, dst_v_stride
        );
    }

    const __m128i rgba_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba_shuffle_mask);

    scranrot_transform_framebuffer_to_yuv_impl_fn transform_fn = NULL;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_to_yuv__ssse3_unaligned__rotate_270; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv__ssse3_unaligned__rotate_180; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv__ssse3_unaligned__rotate_90 ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv__ssse3_unaligned__rotate_0  ; break;
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
        transform, &rgba_shuffle_mask_128,
        KERNEL_TILE_WIDTH_PX, KERNEL_TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}


#endif
