#if defined(__x86_64__) || defined(__i386__)


#include <stdbool.h>
#include <stdint.h>
#include <tmmintrin.h>

#include "scranrot.h"
#include "../util.h"
#include "../util-sse2.h"
#include "../generic-kernel-dispatcher.h"
#include "../implementations.h"


enum {
    RGBA32_PIXELS_PER_XMM = 4,

    KERNEL_TILE_WIDTH_PX  = 32,
    KERNEL_TILE_HEIGHT_PX = 32,

    MIN_TILE_WIDTH_PX  = KERNEL_TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = KERNEL_TILE_HEIGHT_PX,
};
_Static_assert(RGBA32_PIXELS_PER_XMM * RGBA32_PIXEL_STRIDE == sizeof(__m128i), "This file assumes an XMM register holds 4 RGBA32 pixels.");


// Target: 77,150,29
//
// Similar drill as in get_yuv_u_rbga_coefficients. (We can't simply flip the
// sign here, since 150 won't fit on either side of 0 in signed i8, and we need
// to use maddubs/VPMADDUBSW, which treats one of its operands as signed (i.e.
// these coefficients) as signed.
//
// RBGA (as opposed to RGBA) simplifies the calculation, since A (Alpha) will
// always be multiplied by zero, and so does not care about the
// "pairwise coeffiecients" values
static inline __m128i
get_yuv_y_rbga_coefficients() {
    return _mm_setr_epi8(77,29,1,0,  77,29,1,0,  77,29,1,0,  77,29,1,0);
}
static inline __m128i
get_yuv_y_pairwise_rbga_coefficients() {
    return _mm_setr_epi16(1,150,   1,150,   1,150,   1,150);
}

static inline __m128i
get_yuv_u_rbga_coefficients() {
    // Target (RGB): -43,-85,128.
    // b (128) cannot be represented in a signed i8, so flip the sign on it and
    // its _mm_maddubs_epi16()-paired coefficient, then fold the resulting
    // pairwise i16 products as -(r+b)+(g+a), instead of (r+b)+(g+a).
    return _mm_setr_epi8(+43,-128,-85,0, +43,-128,-85,0, +43,-128,-85,0, +43,-128,-85,0);
}
static inline __m128i
get_yuv_v_rbga_coefficients() {
    // Target (RGB): 128,-107,-21.
    // See comment in get_yuv_u_rbga_coefficients.
    return _mm_setr_epi8(-128,+21,-107,0, -128,+21,-107,0, -128,+21,-107,0, -128,+21,-107,0);
}

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
m128i_pairwise_sum_i16_to_i32(__m128i val) {
    return _mm_madd_epi16(val, _mm_set1_epi16(1));
}

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
get_yuv_u_pairwise_rbga_coefficients(void) {
    return _mm_setr_epi16(-1, 1, -1, 1, -1, 1, -1, 1);
}

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
get_yuv_v_pairwise_rbga_coefficients(void) {
    return _mm_setr_epi16(-1, 1, -1, 1, -1, 1, -1, 1);
}

// TODO: Use unrolled loops for all the transpose and rotation functions.

// SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
// static inline __m128i
// _rgba32_to_yuv_plane_32bpp_unsigned_coefficients(
//     const __m128i *const rgba_in,
//     const __m128i *const coefficients,
//     const uint8_t shr
// ) {
//     return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
//               m128i_i16_pairwise_sum(
//                   _mm_maddubs_epi16(*rgba_in, *coefficients)
//               ),
//               shr
//           );
// }

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
convert_rgba32_to_yuv_y_32bpp(
    const __m128i *const rgba_in,
    const __m128i *const coefficients,
    // coefficients for adjacent byte pairs, after the PMADDUBSW pairings
    // See get_yuv_y_rbga_coefficients for more info.
    const __m128i *const pairwise_coefficients,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const uint8_t shr
) {
    return _mm_srai_epi32(
              _mm_madd_epi16(
                  _mm_maddubs_epi16(*rgba_in, *coefficients),
                  *pairwise_coefficients
              ),
              shr
          );
}

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
    const __m128i *const rgba_in,
    const __m128i *const coefficients,
    // coefficients for adjacent byte pairs, after the PMADDUBSW pairings
    // See get_yuv_u_rbga_coefficients for more info.
    const __m128i *const pairwise_coefficients,
    // any reasonable 8-bit y spec will want 8, but if we e.g. halve the gamut, we will need to shift by 7
    const uint8_t shr
) {
    // We need to represent our values as signed, so we normalize them by adding
    // the max signed absolute value, to take the (post-shr) range to 0:255.
    const __m128i uv_s_to_us_offset_epi32 = _mm_set1_epi32((128 << 8) + 128);

    return _mm_srai_epi32( // Y32 := [_Y32>>shr] => Y32 == [y32, ...]
               _mm_add_epi32( // Y32 := uv_s_to_us_offset(Y32)
                   _mm_madd_epi16(
                       _mm_maddubs_epi16(*rgba_in, *coefficients),
                       *pairwise_coefficients
                   ),
                   uv_s_to_us_offset_epi32
               ),
               shr
          );
}

// SSSE3 replacement for SSE4.1's _mm_packus_epi32.
//
// Safe to use as a replacement as long as the input values fall within [0, INT16_MAX]
static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
packus_epi32_ssse3_assume_0_to_i16max(__m128i a, __m128i b) {
    return _mm_packs_epi32(a, b);
}

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
    const __m128i rgba_in[static 4],
    const __m128i *const coefficients,
    const __m128i *const pairwise_coefficients,
    const uint8_t shr
) {
    // Returns: [ (i16)(a0+a1)/2, (i16)(a2+a3)/2, ...]

    return _mm_srai_epi16( // V16_avg([y,x])
               packus_epi32_ssse3_assume_0_to_i16max( // V16_avg(y+x)
                   m128i_pairwise_sum_i16_to_i32(
                       packus_epi32_ssse3_assume_0_to_i16max( // V16_avg(y)
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients( // V32_yavg
                               &rgba_in[0], coefficients, pairwise_coefficients, shr
                           ),
                           convert_rgba32_to_yuv_plane_32bpp_signed_coefficients(
                               &rgba_in[1], coefficients, pairwise_coefficients, shr
                           )
                       )
                   ),

                   m128i_pairwise_sum_i16_to_i32(
                       packus_epi32_ssse3_assume_0_to_i16max(
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

static inline __m128i SCRANROT_TARGET_SSSE3 SCRANROT_ALWAYS_INLINE
convert_16px_rgba32_to_yuv_8bpp(
    const __m128i rgba_in[static 4],
    const __m128i *const coefficients,
    const __m128i *const pairwise_coefficients,
    const uint8_t shr
) {
    // TODO: Function to get the intermediate A32 value

    return _mm_packus_epi16( // Y8 := [Y16 & 0xFF, Y16_1 & 0xFF] => Y8 == [y8, ...]
              packus_epi32_ssse3_assume_0_to_i16max( // Y16 := [Y32 & 0xFFFF, Y32_1 & 0xFFFF] => Y16 == [y16, ...]
                  convert_rgba32_to_yuv_y_32bpp( // Y32 == [y32, ...]
                      &rgba_in[0], coefficients, pairwise_coefficients, shr
                  ),
                  convert_rgba32_to_yuv_y_32bpp( // Y32_1
                      &rgba_in[1], coefficients, pairwise_coefficients, shr
                  )
              ),

              packus_epi32_ssse3_assume_0_to_i16max( // A16_1
                  convert_rgba32_to_yuv_y_32bpp( // Y32_2
                      &rgba_in[2], coefficients, pairwise_coefficients, shr
                  ),
                  convert_rgba32_to_yuv_y_32bpp( // Y32_3
                      &rgba_in[3], coefficients, pairwise_coefficients, shr
                  )
              )
          );
}

static void SCRANROT_TARGET_SSSE3
transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_270(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    const __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(
        // See comment in get_yuv_y_rbga_coefficients
        rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
    );

    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "270 kernel assumes 32x32 RGBA32 tiles.");

    const __m128i y_coefficients          = get_yuv_y_rbga_coefficients();
    const __m128i u_coefficients          = get_yuv_u_rbga_coefficients();
    const __m128i v_coefficients          = get_yuv_v_rbga_coefficients();
    const __m128i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients();
    const __m128i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients();
    const __m128i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients();

    const int dst_height_px = src_width_px;
    uint8_t *dst_y_start = scranrot_yuv420_y_last_row_start( y_plane, dst_height_px, y_stride);
    uint8_t *dst_u_start = scranrot_yuv420_uv_last_row_start(u_plane, dst_height_px, u_stride);
    uint8_t *dst_v_start = scranrot_yuv420_uv_last_row_start(v_plane, dst_height_px, v_stride);


    for (int y = 0; y <= src_height_px - 32; y += 32) {
        for (int x = 0; x <= src_width_px - 32; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                // NOTE: Storing row pairs for u/v in inner loop, and unpackhi/lo into full rows, in
                //       order to only need to sit on 8 of these at a time (since righthand sub-tile
                //       will be combining into _final already), seems to give worse performance.
                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const uint8_t _xi = _x >> 4; // divide by 16
                    __m128i y_8bpp_final[16];
                    const uint8_t *const src_subtile = src + (y+_y)*src_stride_bytes + (x+_x)*RGBA32_PIXEL_STRIDE;

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            }, {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            },
                        };

                        y_8bpp_final[j+0] = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8);
                        y_8bpp_final[j+1] = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
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


                    // Store Y
                    //   (Inner tile)
                    scranrot_sse2_rotate_270_inplace_16x16_8bpp(y_8bpp_final);
                    {
                        uint8_t *dst_y = dst_y_start + (y+_y) - (x+_x)*y_stride;
                        for (int j = 0; j < 16; ++j) {
                            scranrot_sse2_storeu_m128i(dst_y, y_8bpp_final[j]);
                            dst_y -= y_stride;
                        }
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
            scranrot_sse2_rotate_270_inplace_16x16_8bpp(u_i8_4bpp_final);
            {
                uint8_t *dst_u = dst_u_start + (y/2) - (x/2)*u_stride;
                for (int j = 0; j < 16; ++j) {
                    scranrot_sse2_storeu_m128i(dst_u, u_i8_4bpp_final[j]);
                    dst_u -= u_stride;
                }
            }

            // Store V
            scranrot_sse2_rotate_270_inplace_16x16_8bpp(v_i8_4bpp_final);
            {
                uint8_t *dst_v = dst_v_start + (y/2) - (x/2)*v_stride;
                for (int j = 0; j < 16; ++j) {
                    scranrot_sse2_storeu_m128i(dst_v, v_i8_4bpp_final[j]);
                    dst_v -= v_stride;
                }
            }
        }
    }
}

static void SCRANROT_TARGET_SSSE3
transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_180(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    const __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rotate_180_get_modified_rgba_shuffle(
        scranrot_sse2_rgba_shuffle_to_m128i(
            rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
        )
    );

    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "180 kernel assumes 32x32 RGBA32 tiles.");

    const __m128i y_coefficients          = get_yuv_y_rbga_coefficients();
    const __m128i u_coefficients          = get_yuv_u_rbga_coefficients();
    const __m128i v_coefficients          = get_yuv_v_rbga_coefficients();
    const __m128i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients();
    const __m128i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients();
    const __m128i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients();

    uint8_t *dst_y_start = scranrot_yuv420_y_last_row_end( y_plane, src_width_px, src_height_px, y_stride)
                           - sizeof(__m128i);
    uint8_t *dst_u_start = scranrot_yuv420_uv_last_row_end(u_plane, src_width_px, src_height_px, u_stride)
                           - sizeof(__m128i);
    uint8_t *dst_v_start = scranrot_yuv420_uv_last_row_end(v_plane, src_width_px, src_height_px, v_stride)
                           - sizeof(__m128i);


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

                    // const uint8_t _xi          = _x >> 4; // divide by 16
                    // NOTE: 180 uses reversed _x index order here compared to the other rotations
                    const uint8_t _xi_reversed = (16 - _x) >> 4;
                    const uint8_t *const src_subtile = src + (y+_y)*src_stride_bytes + (x+_x)*RGBA32_PIXEL_STRIDE;
                    uint8_t *dst_y = dst_y_start - (y+_y)*y_stride - (x+_x);

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        // NOTE: 180 uses reversed load order here compared to the other rotations
                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            }, {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            },
                        };

                        // Store Y
                        {
                            const __m128i y_8bpp_final_0 = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8);
                            const __m128i y_8bpp_final_1 = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8);

                            scranrot_sse2_storeu_m128i(dst_y, y_8bpp_final_0);
                            dst_y -= y_stride;
                            scranrot_sse2_storeu_m128i(dst_y, y_8bpp_final_1);
                            dst_y -= y_stride;
                        }

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        // U
                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi_reversed] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                                     rgba_32bpp_rows_avg, &u_coefficients, &u_pairwise_coefficients, 8
                                                                 );
                        // V
                        v_i16_8bpp_xyavg[j_yavg][_xi_reversed] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                                     rgba_32bpp_rows_avg, &v_coefficients, &v_pairwise_coefficients, 8
                                                                 );
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

            // Store U,V
            {
                uint8_t *dst_u = dst_u_start - ((y/2)*u_stride) - (x/2);
                uint8_t *dst_v = dst_v_start - ((y/2)*v_stride) - (x/2);
                for (int l = 0; l < 16; ++l) {
                    scranrot_sse2_storeu_m128i(dst_u, u_i8_4bpp_final[l]);
                    scranrot_sse2_storeu_m128i(dst_v, v_i8_4bpp_final[l]);
                    dst_u -= u_stride;
                    dst_v -= v_stride;
                }
            }

        }
    }
}

static void SCRANROT_TARGET_SSSE3
transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_90(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    const __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(
        // See comment in get_yuv_y_rbga_coefficients
        rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
    );


    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "90 kernel assumes 32x32 RGBA32 tiles.");

    const __m128i y_coefficients          = get_yuv_y_rbga_coefficients();
    const __m128i u_coefficients          = get_yuv_u_rbga_coefficients();
    const __m128i v_coefficients          = get_yuv_v_rbga_coefficients();
    const __m128i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients();
    const __m128i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients();
    const __m128i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients();

    const int dst_width_px = src_height_px;
    uint8_t *dst_y_start = scranrot_yuv420_y_row_end( y_plane, dst_width_px)
                           - sizeof(__m128i);
    uint8_t *dst_u_start = scranrot_yuv420_uv_row_end(u_plane, dst_width_px)
                           - sizeof(__m128i);
    uint8_t *dst_v_start = scranrot_yuv420_uv_row_end(v_plane, dst_width_px)
                           - sizeof(__m128i);

    for (int y = 0; y <= src_height_px - 32; y += 32) {
        for (int x = 0; x <= src_width_px - 32; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            __m128i u_i8_4bpp_final[16];
            __m128i v_i8_4bpp_final[16];

            for (int _y = 0; _y < 32; _y += 16) {

                // NOTE: Storing row pairs for u/v in inner loop, and unpackhi/lo into full rows, in
                //       order to only need to sit on 8 of these at a time (since righthand sub-tile
                //       will be combining into _final already), seems to give worse performance.
                __m128i u_i16_8bpp_xyavg[8][2];
                __m128i v_i16_8bpp_xyavg[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const uint8_t _xi = _x >> 4; // divide by 16
                    __m128i y_8bpp_final[16];
                    const uint8_t *const src_subtile = src + (y+_y)*src_stride_bytes + (x+_x)*RGBA32_PIXEL_STRIDE;

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            }, {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            },
                        };


                        y_8bpp_final[j+0] = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8);
                        y_8bpp_final[j+1] = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8);

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
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
                    {
                        uint8_t *dst_y = dst_y_start - (y+_y) + (x+_x)*y_stride;
                        scranrot_sse2_rotate_90_inplace_16x16_8bpp(y_8bpp_final);
                        for (int j = 0; j < 16; ++j) {
                            scranrot_sse2_storeu_m128i(dst_y, y_8bpp_final[j]);
                            dst_y += y_stride;
                        }
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
            scranrot_sse2_rotate_90_inplace_16x16_8bpp(u_i8_4bpp_final);
            {
                uint8_t *dst_u = dst_u_start - (y/2) + (x/2)*u_stride;
                for (int j = 0; j < 16; ++j) {
                    scranrot_sse2_storeu_m128i(dst_u, u_i8_4bpp_final[j]);
                    dst_u += u_stride;
                }
            }

            // Store V
            scranrot_sse2_rotate_90_inplace_16x16_8bpp(v_i8_4bpp_final);
            {
                uint8_t *dst_v = dst_v_start - (y/2) + (x/2)*v_stride;
                for (int j = 0; j < 16; ++j) {
                    scranrot_sse2_storeu_m128i(dst_v, v_i8_4bpp_final[j]);
                    dst_v += v_stride;
                }
            }

        }
    }
}

static void SCRANROT_TARGET_SSSE3
transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_0(
    const uint8_t *restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes,
    uint8_t *restrict y_plane, int y_stride,
    uint8_t *restrict u_plane, int u_stride,
    uint8_t *restrict v_plane, int v_stride,
    const uint32_t rgba32_shuffle_mask
) {
    const __m128i rgba32_shuffle_mask_128 = scranrot_sse2_rgba_shuffle_to_m128i(
        // See comment in get_yuv_y_rbga_coefficients
        rgba32_shuffle_to_rbga32_shuffle(rgba32_shuffle_mask)
    );


    _Static_assert(KERNEL_TILE_WIDTH_PX == 32 && KERNEL_TILE_HEIGHT_PX == 32, "0 kernel assumes 32x32 RGBA32 tiles.");

    const __m128i y_coefficients          = get_yuv_y_rbga_coefficients();
    const __m128i u_coefficients          = get_yuv_u_rbga_coefficients();
    const __m128i v_coefficients          = get_yuv_v_rbga_coefficients();
    const __m128i y_pairwise_coefficients = get_yuv_y_pairwise_rbga_coefficients();
    const __m128i u_pairwise_coefficients = get_yuv_u_pairwise_rbga_coefficients();
    const __m128i v_pairwise_coefficients = get_yuv_v_pairwise_rbga_coefficients();


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

                    const uint8_t _xi = _x >> 4; // divide by 16
                    const uint8_t *const src_subtile = src + (y+_y)*src_stride_bytes + (x+_x)*RGBA32_PIXEL_STRIDE;
                    uint8_t *dst_y = y_plane + (y+_y)*y_stride + (x+_x);

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        const __m128i rgba_32bpp[2][4] = { // 4 XMM registers hold one 16px RGBA32 row
                            {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+0)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            }, {
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  0*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  4*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes +  8*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                                _mm_shuffle_epi8( scranrot_sse2_loadu_m128i(src_subtile + (j+1)*src_stride_bytes + 12*RGBA32_PIXEL_STRIDE) , rgba32_shuffle_mask_128),
                            },
                        };

                        // Store Y
                        {
                            const __m128i y_8bpp_final_0 = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[0][0], &y_coefficients, &y_pairwise_coefficients, 8);
                            const __m128i y_8bpp_final_1 = convert_16px_rgba32_to_yuv_8bpp(&rgba_32bpp[1][0], &y_coefficients, &y_pairwise_coefficients, 8);
                            scranrot_sse2_storeu_m128i(dst_y, y_8bpp_final_0);
                            dst_y += y_stride;
                            scranrot_sse2_storeu_m128i(dst_y, y_8bpp_final_1);
                            dst_y += y_stride;
                        }

                        // We average the two rows before converting, to reduce required calculation
                        const __m128i rgba_32bpp_rows_avg[4] = {
                            _mm_avg_epu8(rgba_32bpp[0][0], rgba_32bpp[1][0]),
                            _mm_avg_epu8(rgba_32bpp[0][1], rgba_32bpp[1][1]),
                            _mm_avg_epu8(rgba_32bpp[0][2], rgba_32bpp[1][2]),
                            _mm_avg_epu8(rgba_32bpp[0][3], rgba_32bpp[1][3]),
                        };

                        const uint8_t j_yavg = j/2;
                        u_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &u_coefficients, &u_pairwise_coefficients, 8
                                                        );
                        v_i16_8bpp_xyavg[j_yavg][_xi] = convert_16px_rgba32_to_yuv_uv_xpairavg_i16_8bpp(
                                                            rgba_32bpp_rows_avg, &v_coefficients, &v_pairwise_coefficients, 8
                                                        );
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

            // Store U,V
            {
                uint8_t *dst_u = u_plane + (y/2)*u_stride + (x/2);
                uint8_t *dst_v = v_plane + (y/2)*v_stride + (x/2);
                for (int l = 0; l < 16; ++l) {
                    scranrot_sse2_storeu_m128i(dst_u, u_i8_4bpp_final[l]);
                    scranrot_sse2_storeu_m128i(dst_v, v_i8_4bpp_final[l]);
                    dst_u += u_stride;
                    dst_v += v_stride;
                }
            }

        }
    }
}


bool
scranrot_transform_framebuffer_to_yuv420_ssse3(
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
        return scranrot_transform_framebuffer_to_yuv420_fallback(
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
        transform_fn = transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_270; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_180; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_90 ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv420__ssse3_unaligned__rotate_0  ; break;
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
