#if defined(__x86_64__) || defined(__i386__)


#include <tmmintrin.h>

#include "scranrot.h"
#include "./ssse3-backend.hpp"
#include "../generic-dispatch.hpp"
#include "../types.hpp"

using namespace scranrot::internal::yuv420;


enum {
    TILE_WIDTH_PX  = 32,
    TILE_HEIGHT_PX = 32,

    MIN_TILE_WIDTH_PX  = TILE_WIDTH_PX,
    MIN_TILE_HEIGHT_PX = TILE_HEIGHT_PX,
};


struct Rotate270 {
    static constexpr scranrot_transform TRANSFORM = SCRANROT_TRANSFORM_270;
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = false;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = false;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = false;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = true;


    // 90-rotation dst starts at the right edge and moves backwards, so the current (sub-)tile
    // is always positioned "behind" us.
    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto y_stride, Point src_max, auto /*subtile_size*/) {
        const int dst_height_px = src_max.x + 1;
        return y_plane + (dst_height_px - 1)*y_stride;
    }
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto uv_stride, Point src_max, auto /*subtile_size*/) {
        const int dst_height_px = src_max.x + 1;
        return uv_plane + (dst_height_px/2 - 1)*uv_stride;
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  + src.y       - (src.x       * y_stride);
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start + (src.y / 2) - ((src.x / 2) * uv_stride);
    }
};

struct Rotate90 {
    static constexpr scranrot_transform TRANSFORM = SCRANROT_TRANSFORM_90;
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = false;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = false;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = true;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = false;


    // 90-rotation dst starts at the right edge and moves backwards, so the current (sub-)tile
    // is always positioned "behind" us.
    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto /*y_stride*/, Point src_max, auto subtile_size) {
        const int dst_width_px = src_max.y + 1;
        return y_plane  +  dst_width_px      - subtile_size;
    }
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto /*uv_stride*/, Point src_max, auto subtile_size) {
        const int dst_width_px = src_max.y + 1;
        return uv_plane + (dst_width_px / 2) - subtile_size;
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  -  src.y      + ( src.x      * y_stride);
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start - (src.y / 2) + ((src.x / 2) * uv_stride);
    }
};

struct Rotate180 {
    static constexpr scranrot_transform TRANSFORM = SCRANROT_TRANSFORM_180;
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = true;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = true;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = true;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = true;


    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto y_stride, Point src_max, auto subtile_size) {
        return y_plane + src_max.y * y_stride + ((src_max.x+1) - subtile_size);
    }
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto uv_stride, Point src_max, auto subtile_size) {
        return uv_plane + src_max.y/2 * uv_stride + ((src_max.x+1)/2 - subtile_size);
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  - (src.y       * y_stride ) - src.x;
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start - ((src.y / 2) * uv_stride) - (src.x / 2);
    }
};

struct Rotate0 {
    static constexpr scranrot_transform TRANSFORM = SCRANROT_TRANSFORM_NORMAL;
    static constexpr bool SHOULD_STORE_Y_IMMEDIATELY     = true;
    static constexpr bool WRITE_SUB_TILE_COLS_IN_REVERSE = false;
    static constexpr bool DST_COLS_WALK_BACKWARDS        = false;
    static constexpr bool DST_ROWS_WALK_BACKWARDS        = false;


    static inline u8 *get_dst_yuv_y_walk_start_address(u8 *y_plane, auto /*y_stride*/, Point /*src_max*/, auto /*subtile_size*/) {
        return y_plane;
    }
    static inline u8 *get_dst_yuv_uv_walk_start_address(u8 *uv_plane, auto /*uv_stride*/, Point /*src_max*/, auto /*subtile_size*/) {
        return uv_plane;
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_y_addr_from_start_addr(u8 *y_start, auto y_stride, Point src) {
        return y_start  + src.x       + (src.y       * y_stride);
    }
    SCRANROT_ALWAYS_INLINE
    static inline u8 *get_dst_yuv_uv_addr_from_start_addr(u8 *uv_start, auto uv_stride, Point src) {
        return uv_start + (src.x / 2) + ((src.y / 2) * uv_stride);
    }
};

template<typename Backend, typename Rotation>
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
    const u32 rgba32_shuffle_mask_u32
) {
    using Coefficients = Backend::Coefficients;
    using ShuffleMask  = Backend::ShuffleMask;
    using Rgba16px     = Backend::Rgba16px;
    using Rgba16px_Y   = Backend::Rgba16px_Y;
    using Rgba16px_UV  = Backend::Rgba16px_UV;
    using Rgba32px_UV  = Backend::Rgba32px_UV;

    const ShuffleMask  rgba32_shuffle_mask = Backend::template get_rgba32_shuffle_mask<Rotation>(rgba32_shuffle_mask_u32);
    const Coefficients coefficients        = Backend::get_yuv_coefficients();

    const Point src_px_max = {
        .x = src_width_px - 1,
        .y = src_height_px - 1
    };

    u8 *dst_y_start = Rotation::get_dst_yuv_y_walk_start_address(y_plane, y_stride, src_px_max, sizeof(Rgba16px_Y));
    u8 *dst_u_start = Rotation::get_dst_yuv_uv_walk_start_address(u_plane, u_stride, src_px_max, sizeof(Rgba32px_UV));
    u8 *dst_v_start = Rotation::get_dst_yuv_uv_walk_start_address(v_plane, v_stride, src_px_max, sizeof(Rgba32px_UV));

    static_assert(TILE_WIDTH_PX == 32 && TILE_HEIGHT_PX == 32, "Kernel assumes 32x32 RGBA32 tiles.");

    for (int y = 0; y < src_height_px; y += 32) {
        for (int x = 0; x < src_width_px; x += 32) {

            // y (yuv) is 2x bpp, so we transpose and store already in inner loop
            // NOTE: bpp here is for u/v-plane pixels, which are at half res for yuv420
            Rgba32px_UV rgba32px_rows_u[16];
            Rgba32px_UV rgba32px_rows_v[16];

            for (int _y = 0; _y < 32; _y += 16) {

                Rgba16px_UV rgba16px_rows_u[8][2];
                Rgba16px_UV rgba16px_rows_v[8][2];

                for (int _x = 0; _x < 32; _x += 16) {

                    const u8 _xi = [&] {
                        if constexpr (Rotation::WRITE_SUB_TILE_COLS_IN_REVERSE) {
                            return (16 - _x) >> 4;
                        } else  {
                            return _x >> 4; // divide by 16
                        }
                    }();

                    // XXX TODO: Can we omit the declaration entirely for the rotations that don't make use of this?
                    [[maybe_unused]] Rgba16px_Y rgba16px_rows_y[16];

                    const Point src_px = {  (x + _x),  (y + _y)  };

                    u8 const *const src_subtile = src + (src_px.y * src_stride_bytes) + (src_px.x * RGBA32_PIXEL_STRIDE);
                    u8       *      dst_y       = Rotation::get_dst_yuv_y_addr_from_start_addr(dst_y_start, y_stride, src_px);

                    for (int j = 0; j < 16; j += 2) { // += 2 so we can average u and v more efficiently

                        // XXX TODO: This should be decided based on the rotation by *this* function,
                        // not by the Rotation type itself.
                        constexpr bool LOAD_REVERSED = Rotation::WRITE_SUB_TILE_COLS_IN_REVERSE;
                        const Rgba16px rgba16px_row_0 = Backend::template load_shuffled_rgba16px<LOAD_REVERSED>(src_subtile + (j+0)*src_stride_bytes, rgba32_shuffle_mask);
                        const Rgba16px rgba16px_row_1 = Backend::template load_shuffled_rgba16px<LOAD_REVERSED>(src_subtile + (j+1)*src_stride_bytes, rgba32_shuffle_mask);

                        {
                            const Rgba16px_Y rgba16px_row_0_y = Backend::rgba16px_to_y(rgba16px_row_0, coefficients);
                            const Rgba16px_Y rgba16px_row_1_y = Backend::rgba16px_to_y(rgba16px_row_1, coefficients);

                            if constexpr (Rotation::SHOULD_STORE_Y_IMMEDIATELY) {
                                Backend::store_rgba16px_y(dst_y, rgba16px_row_0_y);
                                if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                                    dst_y -= y_stride;
                                } else {
                                    dst_y += y_stride;
                                }

                                Backend::store_rgba16px_y(dst_y, rgba16px_row_1_y);
                                if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                                    dst_y -= y_stride;
                                } else {
                                    dst_y += y_stride;
                                }
                            } else {
                                rgba16px_rows_y[j+0] = rgba16px_row_0_y;
                                rgba16px_rows_y[j+1] = rgba16px_row_1_y;
                            }
                        }

                        // We average the two rows before converting, to reduce required calculation
                        const Rgba16px rgba16px_row_0_1_average = Backend::average_rgba16px(rgba16px_row_0, rgba16px_row_1);

                        const u8 j_yavg = j/2;
                        rgba16px_rows_u[j_yavg][_xi] = Backend::rgba16px_to_u_xpairavg(
                                                            rgba16px_row_0_1_average, coefficients
                                                        );
                        rgba16px_rows_v[j_yavg][_xi] = Backend::rgba16px_to_v_xpairavg(
                                                            rgba16px_row_0_1_average, coefficients
                                                        );
                    }

                    if constexpr(Rotation::SHOULD_STORE_Y_IMMEDIATELY == false) {
                        // Store Y (Inner tile)
                        Backend::template rotate_rgba16px_y_tile_in_place<Rotation>(rgba16px_rows_y);
                        for (int j = 0; j < 16; ++j) {
                            Backend::store_rgba16px_y(dst_y, rgba16px_rows_y[j]);
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
                    rgba32px_rows_u[(_y/2)+k] = Backend::rgba16px_uv_to_rgba32px_uv(
                                                     rgba16px_rows_u[k][0],
                                                     rgba16px_rows_u[k][1]
                                                );
                    rgba32px_rows_v[(_y/2)+k] = Backend::rgba16px_uv_to_rgba32px_uv(
                                                     rgba16px_rows_v[k][0],
                                                     rgba16px_rows_v[k][1]
                                                );
                }

            }

            SCRANROT_ASSERT((y==0||x==0) || (y%16==0 && x%16==0));

            const Point src_px   = { x, y };

            // Store U
            Backend::template rotate_rgba32px_uv_tile_in_place<Rotation>(rgba32px_rows_u);
            {
                u8 *dst_u = Rotation::get_dst_yuv_uv_addr_from_start_addr(dst_u_start, u_stride, src_px);
                for (int l = 0; l < 16; ++l) {
                    Backend::store_rgba32px_uv(dst_u, rgba32px_rows_u[l]);
                    if constexpr (Rotation::DST_ROWS_WALK_BACKWARDS) {
                        dst_u -= u_stride;
                    } else {
                        dst_u += u_stride;
                    }
                }
            }

            // Store V
            Backend::template rotate_rgba32px_uv_tile_in_place<Rotation>(rgba32px_rows_v);
            {
                u8 *dst_v = Rotation::get_dst_yuv_uv_addr_from_start_addr(dst_v_start, v_stride, src_px);
                for (int l = 0; l < 16; ++l) {
                    Backend::store_rgba32px_uv(dst_v, rgba32px_rows_v[l]);
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

    transform_framebuffer_to_yuv_impl_fn transform_fn = nullptr;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<YUV420BackendSSSE3, Rotate270>; break;
    case SCRANROT_TRANSFORM_180:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<YUV420BackendSSSE3, Rotate180>; break;
    case SCRANROT_TRANSFORM_90:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<YUV420BackendSSSE3, Rotate90> ; break;
    case SCRANROT_TRANSFORM_NORMAL:
        transform_fn = transform_framebuffer_to_yuv_ssse3_impl<YUV420BackendSSSE3, Rotate0>  ; break;
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
        transform, rgba_shuffle_mask,
        TILE_WIDTH_PX, TILE_HEIGHT_PX,

        // OUT:
        dst_y, dst_y_stride,
        dst_u, dst_u_stride,
        dst_v, dst_v_stride
    );
}


#endif
