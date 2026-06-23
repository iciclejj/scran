#ifndef SCRANROT_YUV420_AVX2_BACKEND_HPP
#define SCRANROT_YUV420_AVX2_BACKEND_HPP

#include <immintrin.h>
#if defined(__x86_64__) || defined(__i386__)


#include <tmmintrin.h>

#include "../backends.hpp"
#include "../sse2.hpp"


namespace scranrot::internal {
    template<>
    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    inline __m256i
    load_unaligned<__m256i>(const void *src) {
        return _mm256_loadu_si256(static_cast<const __m256i_u *>(src));
    }
}

namespace scranrot::internal::yuv420 {

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
    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    static inline __m256i
    convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2(
        const __m256i &rgba_in,
        const __m256i &coefficients_a,
        const __m256i &coefficients_b,
        // Should probably always be 1. Required as an arg to not re-initialize every time.
        const __m256i &hadamard_scaler,
        // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
        const u8 shr
    ) {
        return _mm256_srai_epi32( // average of results
                  _mm256_add_epi32( // sum of results
                      _mm256_madd_epi16( // coefficients_a result
                          _mm256_maddubs_epi16(rgba_in, coefficients_a),
                          hadamard_scaler
                      ),
                      _mm256_madd_epi16( // coefficients_b result
                          _mm256_maddubs_epi16(rgba_in, coefficients_b),
                          hadamard_scaler
                      )
                  ),
                  shr
              );
    }

    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    static inline __m256i
    convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
        const __m256i &rgba_in,
        const __m256i &coefficients,
        // Should probably always be 1. Required as an arg to not re-initialize every time.
        const __m256i &hadamard_scaler,
        // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
        const u8 shr
    ) {
        // We need to represent our values as signed, so we normalize them by adding
        // the max signed absolute value, to take the (post-shr) range from -127:128 -> 0:255
        // TODO: Can we alter our coefficients instead?
        const __m256i uv_s_to_us_offset_epi32 = _mm256_set1_epi32((128 << 8) + 128);

        return _mm256_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
                   _mm256_add_epi32( // Y32 := uv_s_to_us_offset(Y32)
                       _mm256_madd_epi16( // Y32 := [_Y32=(r*cr+g*cg+b*cb+a*ca), ...] => Y32 == [(+/-)y32<<shr, ...]
                           _mm256_maddubs_epi16(rgba_in, coefficients),
                           hadamard_scaler
                       ),
                       uv_s_to_us_offset_epi32
                   ),
                   shr
              );
    }

    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    static inline __m128i
    ymm_i16_to_xmm_u8(__m256i ymm_i16)
    {
        const __m128i ymm_i16_lo = _mm256_castsi256_si128(ymm_i16);
        const __m128i ymm_i16_hi = _mm256_extracti128_si256(ymm_i16, 1);

        const __m128i xmm_i8 = _mm_packus_epi16(
            ymm_i16_lo,
            ymm_i16_hi
        );

        return xmm_i8;
    }

    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    static inline __m128i
    ymm_i32_to_xmm_u16(__m256i ymm_i32)
    {
        const __m128i ymm_i32_lo = _mm256_castsi256_si128(ymm_i32);
        const __m128i ymm_i32_hi = _mm256_extracti128_si256(ymm_i32, 1);

        const __m128i xmm_i16 = _mm_packus_epi32(
            ymm_i32_lo,
            ymm_i32_hi
        );

        return xmm_i16;
    }


    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    static inline __m128i
    convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
        const __m256i (&rgba_in)[2],
        const __m256i &coefficients,
        // Should probably always be 1. Required as an arg to not re-initialize every time.
        // TODO: Just reinitialize it every time rather than passing this arg around,
        //       both here and for ssse3 etc.
        const __m256i &hadamard_scaler,
        const u8 shr
    ) {
        return _mm_srai_epi16( // i16 (xmm pair average)
                   ymm_i32_to_xmm_u16( // i16 (xmm pair sum)
                       _mm256_madd_epi16( // i32 (ymm pair sum)
                           _mm256_permute4x64_epi64( // i16
                               _mm256_packus_epi32(
                                   convert_rgba32_to_yuv_plane_32bpp_signed_coefficients( // i32
                                       rgba_in[0], coefficients, hadamard_scaler, shr
                                   ),
                                   convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                                       rgba_in[1], coefficients, hadamard_scaler, shr
                                   )
                               ),
                               _MM_SHUFFLE(3, 1, 2, 0)
                           ),
                           _mm256_set1_epi16(1) // NOTE: This one must be identity (all 1). Don't use the passed hadamard_scaler.
                       )
                   ),
                   1 // Divide by 2 to get averages of the madds
               );
    }

    SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
    static inline __m128i
    convert_16px_rgba32_to_yuv_8bpp(
        const __m256i (&rgba_in)[2],
        const __m256i &coefficients_a,
        const __m256i &coefficients_b,
        // Should probably always be 1. Required as an arg to not re-initialize every time.
        const __m256i &hadamard_scaler,
        const u8 shr
    ) {
        return ymm_i16_to_xmm_u8(
                   _mm256_permute4x64_epi64( // i16 yuv plane (ymm)
                       _mm256_packus_epi32(
                           convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // i32 yuv plane (ymm)
                               rgba_in[0], coefficients_a, coefficients_b, hadamard_scaler, shr
                           ),
                           convert_rgba32_to_yuv_plane_32bpp_unsigned_coefficients_x2( // Y32_1
                               rgba_in[1], coefficients_a, coefficients_b, hadamard_scaler, shr
                           )
                       ),
                       _MM_SHUFFLE(3, 1, 2, 0)
                   )
               );
    }


    struct YUV420BackendAVX2 {
        using ShuffleMask  = __m256i;
        using Coefficients = struct {
            struct { // See comment in getter
                __m256i a;
                __m256i b;
            } y;
            __m256i u;
            __m256i v;
        };
        using Rgba16px    = struct { __m256i impl[2]; };
        using Rgba16px_Y  = __m128i;
        using Rgba16px_UV = __m128i; // u16
        using Rgba32px_UV = __m128i; // u8


        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline Rgba16px_Y rgba16px_to_y(
            const Rgba16px &rgba16px, const Coefficients &coefficients
        ) {
            return convert_16px_rgba32_to_yuv_8bpp(
                rgba16px.impl, coefficients.y.a, coefficients.y.b, _mm256_set1_epi16(1), 8
            );
        }
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline Rgba16px_UV rgba16px_to_u_xpairavg(
            const Rgba16px &rgba16px, const Coefficients &coefficients
        ) {
            return convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                rgba16px.impl, coefficients.u, _mm256_set1_epi16(1), 8
            );
        }
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline Rgba16px_UV rgba16px_to_v_xpairavg(
            const Rgba16px &rgba16px, const Coefficients &coefficients
        ) {
            return convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                rgba16px.impl, coefficients.v, _mm256_set1_epi16(1), 8
            );
        }

        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline Rgba32px_UV rgba16px_uv_to_rgba32px_uv(
            const Rgba16px_UV &a, const Rgba16px_UV &b
        ) {
            return _mm_packus_epi16(a, b);
        }


        template<typename Rotation>
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline void rotate_rgba16px_y_tile_in_place(Rgba16px_Y (&tile)[16]) {
            if constexpr (Rotation::TRANSFORM == SCRANROT_TRANSFORM_270) {
                transpose_inplace_16x16_8bpp(tile);
            } else if constexpr (Rotation::TRANSFORM == SCRANROT_TRANSFORM_90) {
                rotate_90_inplace_16x16_8bpp(tile);
            }
        }
        template<typename Rotation>
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline void rotate_rgba32px_uv_tile_in_place(Rgba32px_UV (&tile)[16]) {
            // XXX: Theoretically should also assert that both have 8-bit samples
            rotate_rgba16px_y_tile_in_place<Rotation>(tile);
        }

        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline void store_rgba16px_y(u8 *dst, Rgba16px_Y rgba16px_y) {
            store_unaligned(dst, rgba16px_y);
        }
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline void store_rgba32px_uv(u8 *dst, Rgba32px_UV rgba32px_uv) {
            // XXX: Theoretically should also assert that both have 8-bit samples
            store_unaligned(dst, rgba32px_uv);
        }


        template<bool LoadReversed>
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline Rgba16px load_shuffled_rgba16px(const u8 *src, const ShuffleMask &shuffle_mask) {
            if constexpr (LoadReversed) {
                const __m256i reversed_within_lanes_0 = _mm256_shuffle_epi8(load_unaligned<__m256i>(src+ 8*RGBA32_PIXEL_STRIDE), shuffle_mask);
                const __m256i reversed_within_lanes_1 = _mm256_shuffle_epi8(load_unaligned<__m256i>(src+ 0*RGBA32_PIXEL_STRIDE), shuffle_mask);
                return {
                    _mm256_permute2x128_si256(reversed_within_lanes_0, reversed_within_lanes_0, 0x01),
                    _mm256_permute2x128_si256(reversed_within_lanes_1, reversed_within_lanes_1, 0x01),
                };
            } else {
                return {
                    _mm256_shuffle_epi8(load_unaligned<__m256i>(src+ 0*RGBA32_PIXEL_STRIDE), shuffle_mask),
                    _mm256_shuffle_epi8(load_unaligned<__m256i>(src+ 8*RGBA32_PIXEL_STRIDE), shuffle_mask),
                };
            }
        }

        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline Rgba16px average_rgba16px(const Rgba16px &a, const Rgba16px &b) {
            return {
                _mm256_avg_epu8(a.impl[0], b.impl[0]),
                _mm256_avg_epu8(a.impl[1], b.impl[1]),
            };
        }


        template<typename Rotation>
        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
        static inline ShuffleMask get_rgba32_shuffle_mask(const u32 &mask_u32) {
            __m128i mask = scranrot_sse2_rgba_shuffle_to_m128i(mask_u32);

            if constexpr (Rotation::TRANSFORM == SCRANROT_TRANSFORM_180) {
                mask = scranrot_sse2_rotate_180_get_modified_rgba_shuffle(mask);
            }

            return _mm_broadcastsi128_si256(mask);
        }

        SCRANROT_TARGET_AVX2 SCRANROT_ALWAYS_INLINE
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
                    _mm256_setr_epi8(
                        77,23,29,0,  77,23,29,0,  77,23,29,0,  77,23,29,0,
                        77,23,29,0,  77,23,29,0,  77,23,29,0,  77,23,29,0
                    ),
                    _mm256_setr_epi8(
                        0,127,0,0,   0,127,0,0,   0,127,0,0,   0,127,0,0,
                        0,127,0,0,   0,127,0,0,   0,127,0,0,   0,127,0,0
                    ),
                },
                .u = { _mm256_setr_epi8(
                            -43,-84,127,0, -43,-84,127,0, -43,-84,127,0, -43,-84,127,0,
                            -43,-84,127,0, -43,-84,127,0, -43,-84,127,0, -43,-84,127,0
                        )},
                .v = { _mm256_setr_epi8(
                            127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0,
                            127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0, 127,-106,-21,0
                        ) },
            };
        }

    };

}

#endif
#endif
