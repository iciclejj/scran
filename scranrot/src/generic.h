#ifndef SCRANROT_GENERIC_H
#define SCRANROT_GENERIC_H

#include <stdbool.h>

#include "../include/scranrot.h"


typedef void (*scranrot_transform_framebuffer_impl_fn)(
    const void *restrict src, const int src_width_px, const int src_height_px, const int src_stride_bytes,
    void *restrict dst, const int dst_stride_bytes, const void *rgba32_shuffle
);


SCRANROT_ALWAYS_INLINE
static inline int
_get_max_tileDivisible_height_px(int height, int tile_height) {
    return (height / tile_height) * tile_height;
}

SCRANROT_ALWAYS_INLINE
static inline int
_get_max_tileDivisible_height_remainder_px(int height, int tile_height) {
    return (height % tile_height);
}

SCRANROT_ALWAYS_INLINE
static inline int
_get_max_tileDivisible_width_px(int width, int tile_width) {
    return (width / tile_width) * tile_width;
}

SCRANROT_ALWAYS_INLINE
static inline int
_get_max_tileDivisible_width_remainder_px(int width, int tile_width) {
    return (width % tile_width);
}

// TODO: Should maybe not be inline after all
SCRANROT_ALWAYS_INLINE
static inline bool
transform_framebuffer__generic_dispatcher(
    const void *const restrict src,
    const int src_width_px,
    const int src_height_px,
    const int src_stride_bytes, // Stride of the entire capture source
    void *const restrict dst,
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
        dst_divisible_src_w_offset = dst_stride_bytes * _get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);
        dst_divisible_src_h_offset = 0;
        dst_right_src_edge_crop_offset  = 0;
        dst_bottom_src_edge_crop_offset = (src_height_px - tile_height_px) * RGBA32_PIXEL_STRIDE;
        break;
    case SCRANROT_TRANSFORM_180:
        dst_divisible_src_w_offset = RGBA32_PIXEL_STRIDE * _get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);
        dst_divisible_src_h_offset = dst_stride_bytes    * _get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);
        dst_right_src_edge_crop_offset  = 0;
        dst_bottom_src_edge_crop_offset = 0;
        break;
    case SCRANROT_TRANSFORM_90:
        dst_divisible_src_w_offset = 0;
        dst_divisible_src_h_offset = RGBA32_PIXEL_STRIDE * _get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);
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

    int src_w_px_divisible = _get_max_tileDivisible_width_px(src_width_px, tile_width_px);
    int src_w_px_remaining = _get_max_tileDivisible_width_remainder_px(src_width_px, tile_width_px);

    int src_h_px_divisible = _get_max_tileDivisible_height_px(src_height_px, tile_height_px);
    int src_h_px_remaining = _get_max_tileDivisible_height_remainder_px(src_height_px, tile_height_px);

    { // Tilesize-divisible area
        void *_dst = dst + dst_divisible_src_w_offset + dst_divisible_src_h_offset;
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
        const void       *_src           = src + src_right_src_edge_crop_offset;
        const int         _src_width_px  = tile_width_px;
        const int         _src_height_px = src_h_px_divisible; // Corner is done separately at the end
        void             *_dst           = dst + dst_right_src_edge_crop_offset + dst_divisible_src_h_offset;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }

    if (src_h_px_remaining > 0) { // Bottom edge (relative to src)
        const void       *_src           = src + src_bottom_src_edge_crop_offset;
        const int         _src_width_px  = src_w_px_divisible; // Corner is done separately at the end
        const int         _src_height_px = tile_height_px;
        void             *_dst           = dst + dst_bottom_src_edge_crop_offset + dst_divisible_src_w_offset;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }

    if (src_w_px_remaining > 0 && src_h_px_remaining > 0) { // Bottom-right corner (relative to src)
        const void       *_src           = src + src_right_src_edge_crop_offset + src_bottom_src_edge_crop_offset;
        const int         _src_width_px  = tile_width_px;
        const int         _src_height_px = tile_height_px;
        void             *_dst           = dst + dst_right_src_edge_crop_offset + dst_bottom_src_edge_crop_offset;

        rotation_impl_fn(
            _src, _src_width_px, _src_height_px, src_stride_bytes,
            _dst, dst_stride_bytes, rgba32_shuffle
        );
    }

    return true;
}


#endif
