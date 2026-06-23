#ifndef SCRANROT_YUV420_BACKENDS_HPP
#define SCRANROT_YUV420_BACKENDS_HPP

#if defined(__x86_64__) || defined(__i386__)


#include <tmmintrin.h>

#include "../backends.hpp"
#include "../sse2.hpp"

using namespace scranrot::internal;


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


struct YUV420BackendSSSE3 {
    using StorageT = __m128i;

    using ShuffleMask  = __m128i;
    using Coefficients = struct {
        struct { // See comment in getter
            __m128i a;
            __m128i b;
        } y;
        __m128i u;
        __m128i v;
    };
    using Rgba16px    = struct { __m128i impl[4]; };
    using Rgba16px_Y  = __m128i;
    using Rgba16px_UV = __m128i; // u16


    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline Rgba16px_Y rgba16px_to_y(
        const Rgba16px &rgba16px, const Coefficients &coefficients
    ) {
        return convert_16px_rgba32_to_yuv_8bpp(
            rgba16px.impl, coefficients.y.a, coefficients.y.b, _mm_set1_epi16(1), 8
        );
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline Rgba16px_UV rgba16px_to_u_xpairavg(
        const Rgba16px &rgba16px, const Coefficients &coefficients
    ) {
        return convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
            rgba16px.impl, coefficients.u, _mm_set1_epi16(1), 8
        );
    }
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline Rgba16px_UV rgba16px_to_v_xpairavg(
        const Rgba16px &rgba16px, const Coefficients &coefficients
    ) {
        return convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
            rgba16px.impl, coefficients.v, _mm_set1_epi16(1), 8
        );
    }

    template<typename Rotation>
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline void rotate_8bpp_tile_in_place(StorageT (&tile)[16]) {
        if constexpr (Rotation::TRANSFORM == SCRANROT_TRANSFORM_270) {
            transpose_inplace_16x16_8bpp(tile);
        } else if constexpr (Rotation::TRANSFORM == SCRANROT_TRANSFORM_90) {
            rotate_90_inplace_16x16_8bpp(tile);
        }
    }


    template<bool LoadReversed>
    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline Rgba16px load_shuffled_rgba16px(const u8 *src, const ShuffleMask &shuffle_mask) {
        if constexpr (LoadReversed) {
            return {
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+12*RGBA32_PIXEL_STRIDE), shuffle_mask),
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+ 8*RGBA32_PIXEL_STRIDE), shuffle_mask),
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+ 4*RGBA32_PIXEL_STRIDE), shuffle_mask),
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+ 0*RGBA32_PIXEL_STRIDE), shuffle_mask),
            };
        } else {
            return {
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+ 0*RGBA32_PIXEL_STRIDE), shuffle_mask),
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+ 4*RGBA32_PIXEL_STRIDE), shuffle_mask),
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+ 8*RGBA32_PIXEL_STRIDE), shuffle_mask),
                _mm_shuffle_epi8(load_unaligned<__m128i>(src+12*RGBA32_PIXEL_STRIDE), shuffle_mask),
            };
        }
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline Rgba16px average_rgba16px(const Rgba16px &a, const Rgba16px &b) {
        return {
            _mm_avg_epu8(a.impl[0], b.impl[0]),
            _mm_avg_epu8(a.impl[1], b.impl[1]),
            _mm_avg_epu8(a.impl[2], b.impl[2]),
            _mm_avg_epu8(a.impl[3], b.impl[3]),
        };
    }


    template<typename Rotation>
    SCRANROT_ALWAYS_INLINE
    static inline ShuffleMask get_rgba32_shuffle_mask(const u32 &mask_u32) {
        __m128i mask = scranrot_sse2_rgba_shuffle_to_m128i(mask_u32);

        if constexpr (Rotation::TRANSFORM == SCRANROT_TRANSFORM_180) {
            mask = scranrot_sse2_rotate_180_get_modified_rgba_shuffle(mask);
        }

        return mask;
    }

    SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
    static inline Coefficients get_yuv_coefficients() {
        return {
            .y = {
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
                _mm_setr_epi8(77,23,29,0,  77,23,29,0,  77,23,29,0,  77,23,29,0),
                _mm_setr_epi8(0,127,0,0,   0,127,0,0,   0,127,0,0,   0,127,0,0),
            },
            .u = { _mm_setr_epi8(-43,-84,127,0, -43,-84,127,0, -43,-84,127,0, -43,-84,127,0)},
            .v = { _mm_setr_epi8(127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0) },
        };
    }

};

#endif
#endif
