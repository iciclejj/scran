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

static inline BLRectI
blboxi_to_blrecti(BLBoxI box) {
    return (BLRectI) {
        box.x0,
        box.y0,
        blboxi_width(box),
        blboxi_height(box),
    };
}


#endif
