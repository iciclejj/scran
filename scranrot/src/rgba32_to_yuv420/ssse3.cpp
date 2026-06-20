#if defined(__x86_64__) || defined(__i386__)


#include <tmmintrin.h>

#include "scranrot.h"
#include "../sse2.hpp"
#include "../generic-dispatch.hpp"
#include "../types.hpp"

using namespace scranrot::internal;


enum {
    RGBA32_PIXELS_PER_XMM = 4,

    TILE_WIDTH_PX  = 32,
    TILE_HEIGHT_PX = 32,

    MIN_TILE_WIDTH_PX  = TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = TILE_HEIGHT_PX,
};
static_assert(RGBA32_PIXELS_PER_XMM * RGBA32_PIXEL_STRIDE == sizeof(__m128i), "This file assumes an XMM register holds 4 RGBA32 pixels.");


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
get_yuv_y_coefficients_a() {
    return _mm_setr_epi8(77,23,29,0,  77,23,29,0,  77,23,29,0,  77,23,29,0);
}
static inline __m128i
get_yuv_y_coefficients_b() {
    return _mm_setr_epi8(0,127,0,0,   0,127,0,0,   0,127,0,0,   0,127,0,0);
}

static inline __m128i
get_yuv_u_coefficients() {
    return _mm_setr_epi8(-43,-84,127,0, -43,-84,127,0, -43,-84,127,0, -43,-84,127,0);
}
static inline __m128i
get_yuv_v_coefficients() {
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
transpose_inplace_16x16_8bpp(
    __m128i (&arg)[16]
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
rotate_90_inplace_16x16_8bpp(
    __m128i (&arg)[16]
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

// SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
// static inline __m128i
// _rgba32_to_yuv_plane_32bpp_unsigned_coefficients(
//     const __m128i *const rgba_in,
//     const __m128i *const coefficients,
//     // Should probably always be 1. Required as an arg to not re-initialize every time.
//     const __m128i *const hadamard_scaler,
//     const u8 shr
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
SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2(
    const __m128i &rgba_in,
    const __m128i &coefficients_a,
    const __m128i &coefficients_b,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i &hadamard_scaler,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const u8 shr
) {
    return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
              _mm_add_epi32(
                  _mm_madd_epi16( // Y32 := [_Y32=(r*cr+g*cg+b*cb+a*ca), ...] => Y32 == [y32<<shr, ...]
                      _mm_maddubs_epi16(rgba_in, coefficients_a),
                      hadamard_scaler
                  ),
                  _mm_madd_epi16(
                      _mm_maddubs_epi16(rgba_in, coefficients_b),
                      hadamard_scaler
                  )
              ),
              shr
          );
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
    const __m128i &rgba_in,
    const __m128i &coefficients,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i &hadamard_scaler,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const u8 shr
) {
    // We need to represent our values as signed, so we normalize them by adding
    // the max signed absolute value, to take the (post-shr) range from -127:128 -> 0:255
    // TODO: Can we alter our coefficients instead?
    const __m128i uv_s_to_us_offset_epi32 = _mm_set1_epi32((128 << 8) + 128);

    return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
               _mm_add_epi32( // Y32 := uv_s_to_us_offset(Y32)
                   _mm_madd_epi16( // Y32 := [_Y32=(r*cr+g*cg+b*cb+a*ca), ...] => Y32 == [(+/-)y32<<shr, ...]
                       _mm_maddubs_epi16(rgba_in, coefficients),
                       hadamard_scaler
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
packus_epi32_ssse3_assume_0_to_i16max(__m128i a, __m128i b) {
    return _mm_packs_epi32(a, b);
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
    const __m128i (&rgba_in)[4],
    const __m128i &coefficients,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i &hadamard_scaler,
    const u8 shr
) {
    // Returns: [ (i16)(a0+a1)/2, (i16)(a2+a3)/2, ...]

    const __m128i hadamard_identity = _mm_set1_epi16(1);

    return _mm_srai_epi16( // V16_avg([y,x])
               packus_epi32_ssse3_assume_0_to_i16max( // V16_avg(y+x)
                   _mm_madd_epi16 ( // V32_avg(y+x)
                       packus_epi32_ssse3_assume_0_to_i16max( // V16_avg(y)
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients( // V32_yavg
                               rgba_in[0], coefficients, hadamard_scaler, shr
                           ),
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               rgba_in[1], coefficients, hadamard_scaler, shr
                           )
                       ),
                       hadamard_identity // NOTE: This one must be identity (all 1). Don't use the passed hadamard_scaler.
                   ),

                   _mm_madd_epi16 (
                       packus_epi32_ssse3_assume_0_to_i16max(
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               rgba_in[2], coefficients, hadamard_scaler, shr
                           ),
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               rgba_in[3], coefficients, hadamard_scaler, shr
                           )
                       ),
                       hadamard_identity
                   )
               ),
               1 // Divide by 2 to get averages of the madds
           );
}

SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
static inline __m128i
convert_16px_rgba32_to_yuv_8bpp(
    const __m128i (&rgba_in)[4],
    const __m128i &coefficients_a,
    const __m128i &coefficients_b,
    // Should probably always be 1. Required as an arg to not re-initialize every time.
    const __m128i &hadamard_scaler,
    const u8 shr
) {
    // TODO: Function to get the intermediate A32 value

    return _mm_packus_epi16( // Y8 := [Y16 & 0xFF, Y16_1 & 0xFF] => Y8 == [y8, ...]
              packus_epi32_ssse3_assume_0_to_i16max( // Y16 := [Y32 & 0xFFFF, Y32_1 & 0xFFFF] => Y16 == [y16, ...]
                  convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32 == [y32, ...]
                      rgba_in[0], coefficients_a, coefficients_b, hadamard_scaler, shr
                  ),
                  convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_1
                      rgba_in[1], coefficients_a, coefficients_b, hadamard_scaler, shr
                  )
              ),

              packus_epi32_ssse3_assume_0_to_i16max( // A16_1
                  convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_2
                      rgba_in[2], coefficients_a, coefficients_b, hadamard_scaler, shr
                  ),
                  convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_3
                      rgba_in[3], coefficients_a, coefficients_b, hadamard_scaler, shr
                  )
              )
          );
}


struct Rotate270 {
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = false;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = false;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = false;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = true;


    // 90-rotation dst starts at the right edge and moves backwards, so the current (sub-)tile
    // is always positioned "behind" us.
    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto y_stride, Point src_max, auto /*subtile_size*/) {
        const int dst_height_px = src_max.x + 1;
        return y_plane + (dst_height_px - 1)*y_stride;
    }
    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto uv_stride, Point src_max, auto /*subtile_size*/) {
        const int dst_height_px = src_max.x + 1;
        return uv_plane + (dst_height_px/2 - 1)*uv_stride;
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  + src.y       - (src.x       * y_stride);
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start + (src.y / 2) - ((src.x / 2) * uv_stride);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_in_place_16x16_8bpp(auto &rows_i8_4bpp) {
        transpose_inplace_16x16_8bpp(rows_i8_4bpp);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline __m128i get_modified_shuffle_mask(__m128i mask) {
        return mask;
    }
};

struct Rotate90 {
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = false;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = false;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = true;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = false;


    // 90-rotation dst starts at the right edge and moves backwards, so the current (sub-)tile
    // is always positioned "behind" us.
    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto /*y_stride*/, Point src_max, auto subtile_size) {
        const int dst_width_px = src_max.y + 1;
        return y_plane  +  dst_width_px      - subtile_size;
    }
    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto /*uv_stride*/, Point src_max, auto subtile_size) {
        const int dst_width_px = src_max.y + 1;
        return uv_plane + (dst_width_px / 2) - subtile_size;
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  -  src.y      + ( src.x      * y_stride);
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start - (src.y / 2) + ((src.x / 2) * uv_stride);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_in_place_16x16_8bpp(auto &rows_i8_4bpp) {
        rotate_90_inplace_16x16_8bpp(rows_i8_4bpp);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline __m128i get_modified_shuffle_mask(__m128i mask) {
        return mask;
    }
};

struct Rotate180 {
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = true;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = true;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = true;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = true;


    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto y_stride, Point src_max, auto subtile_size) {
        return y_plane + src_max.y * y_stride + ((src_max.x+1) - subtile_size);
    }
    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto uv_stride, Point src_max, auto subtile_size) {
        return uv_plane + src_max.y/2 * uv_stride + ((src_max.x+1)/2 - subtile_size);
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  - (src.y       * y_stride ) - src.x;
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start - ((src.y / 2) * uv_stride) - (src.x / 2);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_in_place_16x16_8bpp(auto &/*rows*/) { }


    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline __m128i get_modified_shuffle_mask(__m128i mask) {
        return scranrot_sse2_rotate_180_get_modified_rgba_shuffle(mask);
    }
};

struct Rotate0 {
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = true;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = false;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = false;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = false;


    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto /*y_stride*/, Point /*src_max*/, auto /*subtile_size*/) {
        return y_plane;
    }
    SCRANROT_TARGET_SSSE3
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto /*uv_stride*/, Point /*src_max*/, auto /*subtile_size*/) {
        return uv_plane;
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  + src.x       + (src.y       * y_stride);
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start + (src.x / 2) + ((src.y / 2) * uv_stride);
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_in_place_16x16_8bpp(auto &/*rows*/) { }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline __m128i get_modified_shuffle_mask(__m128i mask) {
        return mask;
    }
};

template<typename Rotation>
SCRANROT_TARGET_SSSE3
static void
transform_framebuffer_to_yuv_ssse3_impl(
    const u8 *__restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    u8 *__restrict y_plane, int y_stride,
    u8 *__restrict u_plane, int u_stride,
    u8 *__restrict v_plane, int v_stride,
    const void *_rgba32_shuffle_mask_128 // Mask for _mm_shuffle_epi8
) {
    const __m128i rgba32_shuffle_mask_128 =
        Rotation::get_modified_shuffle_mask( load_unaligned<__m128i>(_rgba32_shuffle_mask_128) );

    static_assert(TILE_WIDTH_PX == 32 && TILE_HEIGHT_PX == 32, "All kernels assume 32x32 RGBA32 tiles.");

    const __m128i y_coefficients_a = get_yuv_y_coefficients_a();
    const __m128i y_coefficients_b = get_yuv_y_coefficients_b();
    const __m128i u_coefficients   = get_yuv_u_coefficients();
    const __m128i v_coefficients   = get_yuv_v_coefficients();

    // TODO: Better to just _mm_set1_epi16(1) in each location?
    const __m128i hadam_ident_epi16 = _mm_set1_epi16(1);

    const Point src_px_max = {
        .x = src_width_px - 1,
        .y = src_height_px - 1
    };

    u8 *dst_y_start = Rotation::get_dst_yuv_y_walk_start_address(y_plane, y_stride, src_px_max, sizeof(__m128i));
    u8 *dst_u_start = Rotation::get_dst_yuv_uv_walk_start_address(u_plane, u_stride, src_px_max, sizeof(__m128i));
    u8 *dst_v_start = Rotation::get_dst_yuv_uv_walk_start_address(v_plane, v_stride, src_px_max, sizeof(__m128i));


    for (int y = 0; y < src_height_px; y += 32) {
        for (int x = 0; x < src_width_px; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                // NOTE: See comment in 270/90 handlers at this location. Though not actually tested for 0 or 180.
                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const u8 _xi = [&] {
                        if constexpr (Rotation::WRITE_SUB_TILE_COLS_IN_REVERSE) {
                            return (16 - _x) >> 4;
                        } else  {
                            return _x >> 4; // divide by 16
                        }
                    }();

                    // XXX TODO: Can we omit the declaration entirely for the rotations that don't make use of this?
                    [[maybe_unused]] __m128i y_8bpp_final[16];

                    const Point src_px = {  (x + _x),  (y + _y)  };

                    u8 const *const src_subtile = src + (src_px.y * src_stride_bytes) + (src_px.x * RGBA32_PIXEL_STRIDE);
                    u8       *      dst_y       = Rotation::get_dst_yuv_y_addr_from_start_addr(dst_y_start, y_stride, src_px);

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        constexpr auto _col_index = [](int i) constexpr {
                            if constexpr (Rotation::WRITE_SUB_TILE_COLS_IN_REVERSE) {
                                return (3 - i);
                            } else {
                                return (i);
                            }
                        };
                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+0)*src_stride_bytes + _col_index(0)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+0)*src_stride_bytes + _col_index(1)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+0)*src_stride_bytes + _col_index(2)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+0)*src_stride_bytes + _col_index(3)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            }, {
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+1)*src_stride_bytes + _col_index(0)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+1)*src_stride_bytes + _col_index(1)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+1)*src_stride_bytes + _col_index(2)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( load_unaligned<__m128i>(src_subtile + (j+1)*src_stride_bytes + _col_index(3)*4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            },
                        };

                        {
                            const __m128i y_8bpp_final_0 = convert_16px_rgba32_to_yuv_8bpp(rgba_32bpp[0], y_coefficients_a, y_coefficients_b, hadam_ident_epi16, 8);
                            const __m128i y_8bpp_final_1 = convert_16px_rgba32_to_yuv_8bpp(rgba_32bpp[1], y_coefficients_a, y_coefficients_b, hadam_ident_epi16, 8);

                            if constexpr (Rotation::SHOULD_STORE_Y_IMMEDIATELY) {
                                store_unaligned(dst_y, y_8bpp_final_0);
                                if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                                    dst_y -= y_stride;
                                } else {
                                    dst_y += y_stride;
                                }

                                store_unaligned(dst_y, y_8bpp_final_1);
                                if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                                    dst_y -= y_stride;
                                } else {
                                    dst_y += y_stride;
                                }
                            } else {
                                y_8bpp_final[j+0] = y_8bpp_final_0;
                                y_8bpp_final[j+1] = y_8bpp_final_1;
                            }
                        }

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        const u8 j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, u_coefficients, hadam_ident_epi16, 8
                                                        );
                        v_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, v_coefficients, hadam_ident_epi16, 8
                                                        );
                    }

                    if constexpr(Rotation::SHOULD_STORE_Y_IMMEDIATELY == false) {
                        // Store Y (Inner tile)
                        Rotation::rotate_in_place_16x16_8bpp(y_8bpp_final);
                        for (int j = 0; j < 16; ++j) {
                            store_unaligned(dst_y, y_8bpp_final[j]);
                            if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                                dst_y -= y_stride;
                            } else {
                                dst_y += y_stride;
                            }
                        }
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

            const Point src_px   = { x, y };

            // Store U
            Rotation::rotate_in_place_16x16_8bpp(u_i8_4bpp_final);
            {
                u8 *dst_u = Rotation::get_dst_yuv_uv_addr_from_start_addr(dst_u_start, u_stride, src_px);
                for (int l = 0; l < 16; ++l) {
                    store_unaligned(dst_u, u_i8_4bpp_final[l]);
                    if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                        dst_u -= u_stride;
                    } else {
                        dst_u += u_stride;
                    }
                }
            }

            // Store V
            Rotation::rotate_in_place_16x16_8bpp(v_i8_4bpp_final);
            {
                u8 *dst_v = Rotation::get_dst_yuv_uv_addr_from_start_addr(dst_v_start, v_stride, src_px);
                for (int l = 0; l < 16; ++l) {
                    store_unaligned(dst_v, v_i8_4bpp_final[l]);
                    if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                        dst_v -= v_stride;
                    } else {
                        dst_v += v_stride;
                    }
                }
            }
        }
    }
}


bool
scranrot::internal::transform_framebuffer_to_yuv420_ssse3__unaligned(
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

    const __m128i rgba_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(rgba_shuffle_mask);

    transform_framebuffer_to_yuv_impl_fn transform_fn = nullptr;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<Rotate270>; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<Rotate180>; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<Rotate90> ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<Rotate0>  ; break;
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
        transform, &rgba_shuffle_mask_128,
        TILE_WIDTH_PX, TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}


#endif
