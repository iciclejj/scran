#ifndef SCRAN_UI_H
#define SCRAN_UI_H

#include <assert.h>
#include <math.h>

#include <blend2d/blend2d.h>

#include "atlas.h"


#define UI_COLOR_SELECTION_DEFAULT   UINT32_C(0xFFFFFFFF)
#define UI_COLOR_TEXT_DEFAULT        UINT32_C(0xFFF2F4F8)
#define UI_COLOR_BG_DIM              UINT32_C(0x85000000)
#define UI_COLOR_BACKPLATE           UINT32_C(0xE612161C)
#define UI_COLOR_BACKPLATE_UNFOCUSED UINT32_C(0x9912161C)
#define UI_COLOR_KEYBOARD_MODIFIER   UINT32_C(0xFFFFD166)
#define UI_COLOR_FREEZEFRAME         UINT32_C(0xFF62DDF5)
#define UI_COLOR_VIDEO_CAPTURE       UINT32_C(0xFFFF7575)


static inline int
ui_item_backplate_padding_px(const struct atlas *atlas) {
    return lround(0.2 * atlas_font_height_px(atlas));
}

static inline int
ui_item_width_px(
    const struct atlas *atlas,
    const struct atlas_text_metrics *metrics
) {
    return atlas_metrics_advance_x_px(metrics) + 2 * ui_item_backplate_padding_px(atlas);
}

static inline int
ui_item_height_px(const struct atlas *atlas) {
    assert(atlas_font_height_px(atlas));
    return atlas_font_height_px(atlas) + 2 * ui_item_backplate_padding_px(atlas);
}

static inline BLPointI
ui_item_backplate_origin(
    const struct atlas *atlas,
    const BLPointI pen_origin
) {
    const int padding = ui_item_backplate_padding_px(atlas);
    return (BLPointI){
        .x = pen_origin.x - padding,
        .y = pen_origin.y - padding,
    };
}

static inline BLPointI
ui_item_pen_origin(
    const struct atlas *atlas,
    const BLPointI backplate_origin
) {
    const int padding = ui_item_backplate_padding_px(atlas);
    return (BLPointI){
        .x = backplate_origin.x + padding,
        .y = backplate_origin.y + padding,
    };
}

static inline BLRectI
ui_item_text_rect(
    const struct atlas *atlas,
    const struct atlas_positioned_metrics *text
) {
    return atlas_positioned_metrics_bbox(atlas, text);
}

static inline BLRectI
ui_item_backplate_rect(
    const struct atlas *atlas,
    const struct atlas_positioned_metrics *text
) {
    if (!atlas_metrics_bbox_has_ink(&text->metrics)) {
        return (BLRectI){0};
    }

    const BLPointI origin = ui_item_backplate_origin(atlas, text->pen_origin);

    return (BLRectI){
        .x = origin.x,
        .y = origin.y,
        .w = ui_item_width_px(atlas, &text->metrics),
        .h = ui_item_height_px(atlas),
    };
}

static inline BLRoundRect
ui_item_backplate_round_rect(
    const struct atlas *atlas,
    const struct atlas_positioned_metrics *text
) {
    const BLRectI rect = ui_item_backplate_rect(atlas, text);
    const double r = 0.22 * rect.h;
    return (BLRoundRect){
        .x = rect.x,
        .y = rect.y,
        .w = rect.w,
        .h = rect.h,
        .rx = r,
        .ry = r,
    };
}


#endif
