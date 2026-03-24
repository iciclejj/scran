#ifndef UTIL_BLEND2D_H
#define UTIL_BLEND2D_H

#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#include <blend2d/blend2d.h>

// TODO: Figure out whether 0 height or weight should be allowed.
#define SCRAN_BL_BOX_IS_INVERTED(box) ( \
    box.x0 > box.x1 \
 || box.y0 > box.y1 \
)


#if __has_include(<wayland-client.h>)
    #include <wayland-client.h>

    __attribute__((always_inline))
    static inline void
    _flip_horizontally(struct BLBoxI *box, uint32_t width) {
        box->x0 = width - box->x1;
        box->x1 = width - box->x0;
    }

    // TODO: Look at this again to see whether it handles inverted box. If not,
    // then assert not inverted
    static inline struct BLBoxI
    get_reverse_transform(
        struct BLBoxI box,
        uint32_t source_width,
        uint32_t source_height,
        enum wl_output_transform transform
    ) {
        uint32_t tmp, tmp2;

        switch (transform) {
        case WL_OUTPUT_TRANSFORM_FLIPPED:
            _flip_horizontally(&box, source_width);
        case WL_OUTPUT_TRANSFORM_NORMAL:
            return box;
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            _flip_horizontally(&box, source_width);
        case WL_OUTPUT_TRANSFORM_90:
            tmp = box.x0;
            box.x0 = box.y0;
            box.y0 = source_height - box.x1;
            box.x1 = box.y1;
            box.y1 = source_height - tmp/*x0*/;
            return box;
        case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            _flip_horizontally(&box, source_width);
        case WL_OUTPUT_TRANSFORM_180:
            tmp = box.y0;
            box.y0 = source_height - box.y1;
            box.y1 = source_height - tmp;
            tmp = box.x0;
            box.x0 = source_width - box.x1;
            box.x1 = source_width - tmp;
            return box;
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            _flip_horizontally(&box, source_width);
        case WL_OUTPUT_TRANSFORM_270:
            tmp = box.x0;
            tmp2 = box.x1;
            box.x0 = source_width - box.y1;
            box.x1 = source_width - box.y0;
            box.y0 = tmp;
            box.y1 = tmp2;
            return box;
        }
    }
#endif /* __has_include(<wayland-client.h>) */


static inline int
blboxi_width(BLBoxI box) {
    return box.x1 - box.x0;
}

static inline int
blboxi_height(BLBoxI box) {
    return box.y1 - box.y0;
}

// NOTE: Not overflow-safe
static inline int
blboxi_height_abs_unsafe(BLBoxI box) {
    #ifndef NDEBUG
        if (box.y0 < 0)
            assert(box.y1 <= (INT_MAX - box.y0));
        else if (0 < box.y0)
            assert((INT_MIN + box.y0) <= box.y1);
    #endif

    return abs(box.y1 - box.y0);
}

static inline void
blrecti_deinvert(struct BLRectI *rect)
{
    if (rect->w < 0) {
        rect->w = -rect->w;
        rect->x -= rect->w;
    }

    if (rect->h < 0) {
        rect->h = -rect->h;
        rect->y -= rect->h;
    }
}

static inline struct BLRectI
get_blrecti_deinverted(struct BLRectI rect_in)
{
    const bool x_inverted = rect_in.w < 0;
    const bool y_inverted = rect_in.h < 0;

    return (struct BLRectI) {
        .x = x_inverted ? rect_in.x + rect_in.w : rect_in.x,
        .y = y_inverted ? rect_in.y + rect_in.h : rect_in.y,
        .w = x_inverted ? -rect_in.w : rect_in.w,
        .h = y_inverted ? -rect_in.h : rect_in.h,
    };
}

static inline void
blboxi_deinvert(struct BLBoxI *box)
{
    if (box->x1 < box->x0) {
        int tmp = box->x0;
        box->x0 = box->x1;
        box->x1 = tmp;
    }

    if (box->y1 < box->y0) {
        int tmp = box->y0;
        box->y0 = box->y1;
        box->y1 = tmp;
    }
}

static inline struct BLBoxI
get_blboxi_deinverted(struct BLBoxI box_in)
{
    const bool x_inverted = box_in.x1 < box_in.x0;
    const bool y_inverted = box_in.y1 < box_in.y0;

    return (struct BLBoxI) {
        .x0 = x_inverted ? box_in.x1 : box_in.x0,
        .x1 = x_inverted ? box_in.x0 : box_in.x1,
        .y0 = y_inverted ? box_in.y1 : box_in.y0,
        .y1 = y_inverted ? box_in.y0 : box_in.y1,
    };
}

// NOTE: Not overflow-safe
static inline int
blboxi_width_abs_unsafe(BLBoxI box) {
    #ifndef NDEBUG
        if (box.x0 < 0)
            assert(box.x1 <= (INT_MAX - box.x0));
        else if (0 < box.x0)
            assert((INT_MIN + box.x0) <= box.x1);
    #endif

    return abs(box.x1 - box.x0);
}

static inline BLBoxI
blrecti_to_blboxi(BLRectI rect) {
    return (BLBoxI) {
        rect.x,
        rect.y,
        rect.x + rect.w,
        rect.y + rect.h,
    };
}

static inline BLRectI
blboxi_to_blrecti(BLBoxI box) {
    return (BLRectI) {
        box.x0,
        box.y0,
        blboxi_width(box),
        blboxi_height(box),
    };
}

static inline BLBoxI
get_blboxi_inflated(struct BLBoxI box, int inflation) {
    return (BLBoxI) {
        box.x0 - inflation,
        box.y0 - inflation,
        box.x1 + inflation,
        box.y1 + inflation,
    };
}

// XXX: Just use ceil if we will need math.h for more reasons.
static inline int
blend2d_stroke_ceil(double x)
{
    int int_x = (int)x;
    return (double)int_x < x ? int_x + 1: int_x;
}


#endif
