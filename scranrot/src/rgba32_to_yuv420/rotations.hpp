#ifndef SCRANROT_YUV420_ROTATIONS_HPP
#define SCRANROT_YUV420_ROTATIONS_HPP


#include "scranrot.h"
#include "../types.hpp"
#include "../util.hpp"


namespace scranrot::internal::yuv420 {

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

}


#endif
