#ifndef UTIL_BLEND2D_H
#define UTIL_BLEND2D_H

#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#include <blend2d/blend2d.h>


#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

#define SCRAN_BL_BOX_IS_INVERTED_OR_EMPTY(box) ( \
    box.x0 >= box.x1 \
 || box.y0 >= box.y1 \
)

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

struct scran_rgba32 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};
static inline struct scran_rgba32
get_blrgba32_values(BLRgba32 *color) {
    // : value((r << 16) | (g << 8) | b | (a << 24)) {}
    return (struct scran_rgba32) {
        .r = color->value >> 16,
        .g = color->value >>  8,
        .b = color->value >>  0,
        .a = color->value >> 24,
    };
}

static inline void
set_blrgba32_values(BLRgba32 *color, struct scran_rgba32 scran_color) {
    color->value = (
        scran_color.r << 16 |
        scran_color.g <<  8 |
        scran_color.b <<  0 |
        scran_color.a << 24
    );
}

static inline void
scale_blrgba32_colors(BLRgba32 *color, float scale) {
    struct scran_rgba32 scran_color = get_blrgba32_values(color);
    scran_color.r = ceil(scran_color.r * scale);
    scran_color.g = ceil(scran_color.g * scale);
    scran_color.b = ceil(scran_color.b * scale);
    scran_color.a = ceil(scran_color.a * scale);
    set_blrgba32_values(color, scran_color);
}


static inline bool
blpointi_are_equal(BLPointI a, BLPointI b) {
    return  a.x == b.x && a.y == b.y;
}

static inline bool
blrecti_are_equal(BLRectI a, BLRectI b) {
    return  a.x == b.x &&
            a.y == b.y &&
            a.w == b.w &&
            a.h == b.h
    ;
}

static inline bool
blboxi_are_equal(BLBoxI a, BLBoxI b) {
    return  a.x0 == b.x0 &&
            a.x1 == b.x1 &&
            a.y0 == b.y0 &&
            a.y1 == b.y1
    ;
}

static inline double
blboxd_width(BLBox box) {
    return box.x1 - box.x0;
}

static inline int
blboxd_height(BLBox box) {
    return box.y1 - box.y0;
}

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

static inline void
shift_blboxi(BLBoxI *box, int x_shift, int y_shift) {
    box->x0 += x_shift;
    box->y0 += y_shift;
    box->x1 += x_shift;
    box->y1 += y_shift;
}

static inline BLBoxI
blboxi_intersection_raw(
    BLBoxI a,
    BLBoxI b
) {
    return (BLBoxI) {
        .x0 = MAX(a.x0, b.x0),
        .y0 = MAX(a.y0, b.y0),
        .x1 = MIN(a.x1, b.x1),
        .y1 = MIN(a.y1, b.y1),
    };
}

static inline BLBoxI
blboxi_intersection(
    BLBoxI a,
    BLBoxI b
) {
    const BLBoxI intersection = blboxi_intersection_raw(a, b);

    if (SCRAN_BL_BOX_IS_INVERTED_OR_EMPTY(intersection)) {
        return (BLBoxI){ 0, 0, 0, 0 };
    }

    return intersection;
}

// Operation: a - b
static inline void
get_box_diff_as_4_rects(
    struct BLBoxI a,
    struct BLBoxI b,
    struct BLRectI ret[static 4]
) {
    assert(!SCRAN_BL_BOX_IS_INVERTED(a));
    assert(!SCRAN_BL_BOX_IS_INVERTED(b));

    const struct BLBoxI raw_intersection = blboxi_intersection_raw(a, b);

    if (SCRAN_BL_BOX_IS_INVERTED_OR_EMPTY(raw_intersection)) {
        // No overlap
        ret[0] = blboxi_to_blrecti(a);
        ret[1] = (struct BLRectI){ 0 };
        ret[2] = (struct BLRectI){ 0 };
        ret[3] = (struct BLRectI){ 0 };
        return;
    }

    const struct BLBoxI intersection = raw_intersection;

    const BLRectI left_full = (struct BLRectI) {
        .x = a.x0,
        .w = intersection.x0 - a.x0,
        .y = a.y0,
        .h = a.y1 - a.y0,
    };

    ret[0] = left_full;

    const BLRectI right_full = (struct BLRectI) {
        .x = intersection.x1,
        .w = a.x1 - intersection.x1,
        .y = a.y0,
        .h = a.y1 - a.y0,
    };

    ret[1] = right_full;

    const BLRectI top_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = a.y0,
        .h = intersection.y0 - a.y0,
    };

    ret[2] = top_remaining;

    const BLRectI bottom_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = intersection.y1,
        .h = a.y1 - intersection.y1,
    };

    ret[3] = bottom_remaining;
}

// Operation: a ^ b
static inline void
get_box_symdiff_as_4_rects(
    struct BLBoxI a,
    struct BLBoxI b,
    struct BLRectI ret[static 4]
) {
    assert(!SCRAN_BL_BOX_IS_INVERTED(a));
    assert(!SCRAN_BL_BOX_IS_INVERTED(b));

    const struct BLBoxI raw_intersection = blboxi_intersection_raw(a, b);

    if (SCRAN_BL_BOX_IS_INVERTED_OR_EMPTY(raw_intersection)) {
        // No overlap
        ret[0] = blboxi_to_blrecti(a);
        ret[1] = blboxi_to_blrecti(b);
        ret[2] = (struct BLRectI){ 0 };
        ret[3] = (struct BLRectI){ 0 };
        return;
    }

    const struct BLBoxI intersection = raw_intersection;

    const BLBoxI leftmost  = a.x0 < b.x0 ? a : b;
    const BLRectI left_full = (struct BLRectI) {
        .x = leftmost.x0,
        .w = intersection.x0 - leftmost.x0,
        .y = leftmost.y0,
        .h = leftmost.y1 - leftmost.y0,
    };

    ret[0] = left_full;

    const BLBoxI rightmost = a.x1 > b.x1 ? a : b;
    const BLRectI right_full = (struct BLRectI) {
        .x = intersection.x1,
        .w = rightmost.x1 - intersection.x1,
        .y = rightmost.y0,
        .h = rightmost.y1 - rightmost.y0,
    };

    ret[1] = right_full;

    const BLRectI top_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = MIN(a.y0, b.y0),
        .h = intersection.y0 - MIN(a.y0, b.y0),
    };

    ret[2] = top_remaining;

    const BLRectI bottom_remaining = (struct BLRectI) {
        .x = intersection.x0,
        .w = intersection.x1 - intersection.x0,
        .y = intersection.y1,
        .h = MAX(a.y1, b.y1) - intersection.y1,
    };

    ret[3] = bottom_remaining;
}


#endif
