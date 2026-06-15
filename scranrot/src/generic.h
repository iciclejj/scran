#ifndef SCRANROT_GENERIC_H
#define SCRANROT_GENERIC_H

#include <stdbool.h>

#include "../include/scranrot.h"
#include "./util.h"


typedef void (*scranrot_transform_framebuffer_to_yuv_impl_fn)(
    const uint8_t *restrict src, const int src_width_px, const int src_height_px, const int src_stride_bytes,
    uint8_t *restrict dst_y, const int dst_y_stride,
    uint8_t *restrict dst_u, const int dst_u_stride,
    uint8_t *restrict dst_v, const int dst_v_stride,
    const void *rgba32_shuffle
);

// TODO: Use uint8_t * here too
typedef void (*scranrot_transform_framebuffer_impl_fn)(
    const uint8_t *restrict src, const int src_width_px, const int src_height_px, const int src_stride_bytes,
    uint8_t *restrict dst, const int dst_stride_bytes, const void *rgba32_shuffle
);


SCRANROT_ALWAYS_INLINE
static inline int
get_max_tileDivisible_height_px(int height, int tile_height) {
    return (height / tile_height) * tile_height;
}

SCRANROT_ALWAYS_INLINE
static inline int
get_max_tileDivisible_height_remainder_px(int height, int tile_height) {
    return (height % tile_height);
}

SCRANROT_ALWAYS_INLINE
static inline int
get_max_tileDivisible_width_px(int width, int tile_width) {
    return (width / tile_width) * tile_width;
}

SCRANROT_ALWAYS_INLINE
static inline int
get_max_tileDivisible_width_remainder_px(int width, int tile_width) {
    return (width % tile_width);
}

// TODO: Should maybe not be inline after all
SCRANROT_ALWAYS_INLINE
static inline bool
transform_framebuffer__generic_dispatcher(
    const uint8_t *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes, // Stride of the entire capture source
    uint8_t *const restrict dst,
    const int dst_stride_bytes, // Stride of the final output image

    scranrot_transform_framebuffer_impl_fn rotation_impl_fn,
    enum scranrot_transform transform,
    // rotation_impl_fn-defined format, e.g. __m128i or uint32_t.
    const void *rgba32_shuffle,
    int tile_width_px,
    int tile_height_px
) {
    // dst must be within bounds of the crop created by src_w/h_px_divisible,
    // when they are used for the image dimensions
    //   I.e. rotation_impl_fn(..., src_width=src_w_divisible)
    //        => must do dst+=dst_divisible_src_w_offset.
    int dst_divisible_src_w_offset = 0;
    int dst_divisible_src_h_offset = 0;
    // After we finish rendering the cleanly tile-divisible part of src into
    // dst, we must then draw the remaining edges and corner. We do this by
    // rendering the edges and corner as if they're separate images, cropped
    // so that the end of the tile is perfectly stopping at the end of the
    // entire src's image.
    //
    // The corresponding dst offsets for this crop depends on rotation, which
    // is what these arguments represent.
    int dst_right_src_edge_crop_offset  = 0;
    int dst_bottom_src_edge_crop_offset = 0;

    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        dst_divisible_src_w_offset = dst_stride_bytes * get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);
        dst_divisible_src_h_offset = 0;
        dst_right_src_edge_crop_offset  = 0;
        dst_bottom_src_edge_crop_offset = (src_height_px - tile_height_px) * RGBA32_PIXEL_STRIDE;
        break;
    case SCRANROT_TRANSFORM_180:
        dst_divisible_src_w_offset = RGBA32_PIXEL_STRIDE * get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);
        dst_divisible_src_h_offset = dst_stride_bytes    * get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);
        dst_right_src_edge_crop_offset  = 0;
        dst_bottom_src_edge_crop_offset = 0;
        break;
    case SCRANROT_TRANSFORM_90:
        dst_divisible_src_w_offset = 0;
        dst_divisible_src_h_offset = RGBA32_PIXEL_STRIDE * get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);
        dst_right_src_edge_crop_offset  = (src_width_px - tile_width_px) * dst_stride_bytes;
        dst_bottom_src_edge_crop_offset = 0;
        break;
    case SCRANROT_TRANSFORM_NORMAL:
        dst_divisible_src_w_offset = 0;
        dst_divisible_src_h_offset = 0;
        dst_right_src_edge_crop_offset  = (src_width_px  - tile_width_px)  * RGBA32_PIXEL_STRIDE;
        dst_bottom_src_edge_crop_offset = (src_height_px - tile_height_px) * dst_stride_bytes;
        break;
    default:
        // XXX TODO: Implement flipped
        // TODO: Print error message?
        return false;
    }

    int src_w_px_divisible = get_max_tileDivisible_width_px(src_width_px, tile_width_px);
    int src_w_px_remaining = get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);

    int src_h_px_divisible = get_max_tileDivisible_height_px(src_height_px, tile_height_px);
    int src_h_px_remaining = get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);

    { // Tilesize-divisible area
        uint8_t *_dst = dst + dst_divisible_src_w_offset + dst_divisible_src_h_offset;
        rotation_impl_fn(
            src, src_w_px_divisible, src_h_px_divisible, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }


    //
    // Edge handling
    //
    const int src_right_src_edge_crop_offset  = (src_width_px  - tile_width_px)  * RGBA32_PIXEL_STRIDE;
    const int src_bottom_src_edge_crop_offset = (src_height_px - tile_height_px) * src_stride_bytes;

    if (src_w_px_remaining > 0) { // Right edge (relative to src)
        const uint8_t    *_src           = src + src_right_src_edge_crop_offset;
        const int         _src_width_px  = tile_width_px;
        const int         _src_height_px = src_h_px_divisible; // Corner is done separately at the end
        uint8_t          *_dst           = dst + dst_right_src_edge_crop_offset + dst_divisible_src_h_offset;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }

    if (src_h_px_remaining > 0) { // Bottom edge (relative to src)
        const uint8_t    *_src           = src + src_bottom_src_edge_crop_offset;
        const int         _src_width_px  = src_w_px_divisible; // Corner is done separately at the end
        const int         _src_height_px = tile_height_px;
        uint8_t          *_dst           = dst + dst_bottom_src_edge_crop_offset + dst_divisible_src_w_offset;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }

    if (src_w_px_remaining > 0 && src_h_px_remaining > 0) { // Bottom-right corner (relative to src)
        const uint8_t    *_src           = src + src_right_src_edge_crop_offset + src_bottom_src_edge_crop_offset;
        const int         _src_width_px  = tile_width_px;
        const int         _src_height_px = tile_height_px;
        uint8_t          *_dst           = dst + dst_right_src_edge_crop_offset + dst_bottom_src_edge_crop_offset;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }

    return true;
}

// TODO: Should maybe not be inline after all
SCRANROT_ALWAYS_INLINE
static inline bool
transform_framebuffer_to_yuv420__generic_dispatcher(
    const uint8_t *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes, // Stride of the entire capture source
    uint8_t *const restrict dst,

    scranrot_transform_framebuffer_to_yuv_impl_fn rotation_impl_fn,
    enum scranrot_transform transform,
    const void *rgba32_shuffle, // rotation_impl_fn-defined format, e.g. __m128i or uint32_t.
    int tile_width_px,
    int tile_height_px,

    // XXX TODO: Move this responsibility to the arch-specific caller functions?
    uint8_t **dst_y_, int *dst_y_stride_,
    uint8_t **dst_u_, int *dst_u_stride_,
    uint8_t **dst_v_, int *dst_v_stride_
) {
    const int dst_width_px  = scranrot_get_transformed_width( src_width_px, src_height_px, transform);
    const int dst_height_px = scranrot_get_transformed_height(src_width_px, src_height_px, transform);

    // XXX TODO: Release build error handling of this
    SCRANROT_ASSERT(dst_width_px % 2 == 0 && dst_height_px % 2 == 0 && "scranrot: YUV420 requires width and height to be divisible by 2");

    const int dst_y_stride = dst_width_px;
    const int dst_u_stride = dst_width_px / 2;
    const int dst_v_stride = dst_width_px / 2;
    uint8_t *const dst_y = dst;
    uint8_t *const dst_u = dst_y + dst_y_stride *  dst_height_px;
    uint8_t *const dst_v = dst_u + dst_u_stride * (dst_height_px / 2);

    *dst_y_ = dst_y;
    *dst_u_ = dst_u;
    *dst_v_ = dst_v;
    *dst_y_stride_ = dst_y_stride;
    *dst_u_stride_ = dst_u_stride;
    *dst_v_stride_ = dst_v_stride;

    // dst must be within bounds of the crop created by src_w/h_px_divisible,
    // when they are used for the image dimensions
    //   I.e. rotation_impl_fn(..., src_width=src_w_divisible)
    //        => must do dst+=dst_divisible_src_w_offset.
    //
    // NOTE: We calculate the dst similarly to the non-yuv dispatcher, except we
    // do pixels instead of bytes, so we can multiply it accordingly for y/u/v
    // afterwards. We must also do row/col offsets separately, due to u/v
    // skipping cols in addition to rows.
    int dst_row_divisible_src_w = 0;
    int dst_col_divisible_src_w = 0;
    int dst_row_divisible_src_h = 0;
    int dst_col_divisible_src_h = 0;
    // After we finish rendering the cleanly tile-divisible part of src into
    // dst, we must then draw the remaining edges and corner. We do this by
    // rendering the edges and corner as if they're separate images, cropped
    // so that the end of the tile is perfectly stopping at the end of the
    // entire src's image.
    //
    // The corresponding dst offsets for this crop depends on rotation, which
    // is what these arguments represent.
    int dst_row_right_src_edge_crop  = 0;
    int dst_col_right_src_edge_crop  = 0;
    int dst_row_bottom_src_edge_crop = 0;
    int dst_col_bottom_src_edge_crop = 0;


    // Initialize dst to start at the same pixel that src starts at, in its respective transform.
    // Also pre-calculate the required offsets for the edge-handling runs.
    switch (transform) {
    case SCRANROT_TRANSFORM_270:
        dst_row_divisible_src_w = get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);
        dst_col_divisible_src_w = 0;
        dst_row_divisible_src_h = 0;
        dst_col_divisible_src_h = 0;
        dst_row_right_src_edge_crop = 0;
        dst_col_right_src_edge_crop = 0;
        dst_row_bottom_src_edge_crop = 0;
        dst_col_bottom_src_edge_crop = (src_height_px - tile_height_px);
        break;
    case SCRANROT_TRANSFORM_180:
        dst_row_divisible_src_w = 0;
        dst_col_divisible_src_w = get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);
        dst_row_divisible_src_h = get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);
        dst_col_divisible_src_h = 0;
        dst_row_right_src_edge_crop = 0;
        dst_col_right_src_edge_crop = 0;
        dst_row_bottom_src_edge_crop = 0;
        dst_col_bottom_src_edge_crop = 0;
        break;
    case SCRANROT_TRANSFORM_90:
        dst_row_divisible_src_w = 0;
        dst_col_divisible_src_w = 0;
        dst_row_divisible_src_h = 0;
        dst_col_divisible_src_h = get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);
        dst_row_right_src_edge_crop = (src_width_px - tile_width_px);
        dst_col_right_src_edge_crop = 0;
        dst_row_bottom_src_edge_crop = 0;
        dst_col_bottom_src_edge_crop = 0;
        break;
    case SCRANROT_TRANSFORM_NORMAL:
        dst_row_divisible_src_w = 0;
        dst_col_divisible_src_w = 0;
        dst_row_divisible_src_h = 0;
        dst_col_divisible_src_h = 0;
        dst_row_right_src_edge_crop = 0;
        dst_col_right_src_edge_crop = (src_width_px - tile_width_px);
        dst_row_bottom_src_edge_crop = (src_height_px - tile_height_px);
        dst_col_bottom_src_edge_crop = 0;
        break;
    default:
        // XXX TODO: Implement flipped
        // TODO: Print error message?
        return false;
    }

    int src_w_px_divisible = get_max_tileDivisible_width_px(src_width_px, tile_width_px);
    int src_w_px_remaining = get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);

    int src_h_px_divisible = get_max_tileDivisible_height_px(src_height_px, tile_height_px);
    int src_h_px_remaining = get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);



    { // Tilesize-divisible area
        int row_y = dst_row_divisible_src_h + dst_row_divisible_src_w;
        int col_y = dst_col_divisible_src_h + dst_col_divisible_src_w;

        uint8_t *_dst_y = dst_y + row_y     * dst_y_stride + col_y;
        uint8_t *_dst_u = dst_u + row_y / 2 * dst_u_stride + col_y / 2;
        uint8_t *_dst_v = dst_v + row_y / 2 * dst_v_stride + col_y / 2;

        rotation_impl_fn(
            src, src_w_px_divisible, src_h_px_divisible, src_stride_bytes,
            _dst_y, dst_y_stride,
            _dst_u, dst_u_stride,
            _dst_v, dst_v_stride,
            rgba32_shuffle
        );
    }


    //
    // Edge handling
    //
    const int src_right_src_edge_crop_offset  = (src_width_px  - tile_width_px)  * RGBA32_PIXEL_STRIDE;
    const int src_bottom_src_edge_crop_offset = (src_height_px - tile_height_px) * src_stride_bytes;

    if (src_w_px_remaining > 0) { // Right edge (relative to src)
        const uint8_t *_src           = src + src_right_src_edge_crop_offset;
        const int      _src_width_px  = tile_width_px;
        const int      _src_height_px = src_h_px_divisible; // Corner is done separately at the end

        int row_y = dst_row_right_src_edge_crop + dst_row_divisible_src_h;
        int col_y = dst_col_right_src_edge_crop + dst_col_divisible_src_h;

        uint8_t *_dst_y = dst_y + row_y     * dst_y_stride + col_y;
        uint8_t *_dst_u = dst_u + row_y / 2 * dst_u_stride + col_y / 2;
        uint8_t *_dst_v = dst_v + row_y / 2 * dst_v_stride + col_y / 2;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst_y, dst_y_stride,
            _dst_u, dst_u_stride,
            _dst_v, dst_v_stride,
            rgba32_shuffle
        );
    }

    if (src_h_px_remaining > 0) { // Bottom edge (relative to src)
        const uint8_t *_src           = src + src_bottom_src_edge_crop_offset;
        const int      _src_width_px  = src_w_px_divisible; // Corner is done separately at the end
        const int      _src_height_px = tile_height_px;

        int row_y = dst_row_bottom_src_edge_crop + dst_row_divisible_src_w;
        int col_y = dst_col_bottom_src_edge_crop + dst_col_divisible_src_w;

        uint8_t *_dst_y = dst_y + row_y     * dst_y_stride + col_y;
        uint8_t *_dst_u = dst_u + row_y / 2 * dst_u_stride + col_y / 2;
        uint8_t *_dst_v = dst_v + row_y / 2 * dst_v_stride + col_y / 2;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst_y, dst_y_stride,
            _dst_u, dst_u_stride,
            _dst_v, dst_v_stride,
            rgba32_shuffle
        );
    }

    if (src_w_px_remaining > 0 && src_h_px_remaining > 0) { // Bottom-right corner (relative to src)
        const uint8_t *_src           = src + src_right_src_edge_crop_offset + src_bottom_src_edge_crop_offset;
        const int      _src_width_px  = tile_width_px;
        const int      _src_height_px = tile_height_px;

        int row_y = dst_row_bottom_src_edge_crop + dst_row_right_src_edge_crop;
        int col_y = dst_col_bottom_src_edge_crop + dst_col_right_src_edge_crop;

        uint8_t *_dst_y = dst_y + row_y     * dst_y_stride + col_y;
        uint8_t *_dst_u = dst_u + row_y / 2 * dst_u_stride + col_y / 2;
        uint8_t *_dst_v = dst_v + row_y / 2 * dst_v_stride + col_y / 2;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst_y, dst_y_stride,
            _dst_u, dst_u_stride,
            _dst_v, dst_v_stride,
            rgba32_shuffle
        );
    }

    return true;
}


#endif
