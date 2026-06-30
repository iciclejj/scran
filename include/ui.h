#ifndef SCRAN_UI_H
#define SCRAN_UI_H


#include <assert.h>
#include <limits.h>

#include <blend2d/blend2d.h>

#include "print.h"

enum scran_ui_disable_reason {
    SCRAN_UI_DISABLE_REASON_CAPTURING_VIDEO,
    SCRAN_UI_DISABLE_REASON_RELEASED_FOCUS,
    SCRAN_UI_N_DISABLE_REASONS,
};

enum scran_ui_keymap_item_index {
    SCRAN_UI_KEYMAP_ITEM_I_IMAGE,
    SCRAN_UI_KEYMAP_ITEM_I_VIDEO,
    SCRAN_UI_KEYMAP_ITEM_I_FOCUS,
    SCRAN_UI_KEYMAP_ITEM_I_EXTRA,
    SCRAN_UI_KEYMAP_N_ITEMS,
};

enum scran_ui_keymap_color {
    SCRAN_UI_KEYMAP_COLOR_DEFAULT,
    SCRAN_UI_KEYMAP_COLOR_MOD,
    SCRAN_UI_KEYMAP_COLOR_ALT,
    SCRAN_UI_KEYMAP_COLOR_VIDEO_CAPTURE,
    SCRAN_UI_KEYMAP_N_COLORS,
};

enum scran_ui_keymap_text {
    SCRAN_UI_KEYMAP_TEXT_EXTRA_PRE_INIT_DEFAULT,


    SCRAN_UI_KEYMAP_TEXT_IMAGE_DEFAULT,
    SCRAN_UI_KEYMAP_TEXT_IMAGE_MOD,

    SCRAN_UI_KEYMAP_TEXT_VIDEO_DEFAULT,
    SCRAN_UI_KEYMAP_TEXT_VIDEO_MOD,

    SCRAN_UI_KEYMAP_TEXT_FOCUS_DEFAULT,
    SCRAN_UI_KEYMAP_TEXT_FOCUS_RELEASED_TRAY,
    SCRAN_UI_KEYMAP_TEXT_FOCUS_RELEASED_HELP,


    SCRAN_UI_KEYMAP_TEXT_EMPTY,


    SCRAN_UI_KEYMAP_N_TEXTS,
};

struct scran_ui_keymap_item_lockable_state {
    uint8_t text;
    uint8_t color;
};

struct scran_ui_keymap_item {
    BLImageCore bl_img;
    int width_px; // width of currently displayed text. height_px is in parent.

    struct scran_ui_keymap_item_lockable_state live_state;
    struct scran_ui_keymap_item_lockable_state locked_state;

    uint8_t disable_reason_mask;
    bool locked;
};
static_assert( sizeof((struct scran_ui_keymap_item){}.disable_reason_mask) * CHAR_BIT >= SCRAN_UI_N_DISABLE_REASONS,
               ".disable_reason_mask must fit all possible disable reasons.");

struct scran_ui_keymap {
    struct scran_ui_keymap_item items[SCRAN_UI_KEYMAP_N_ITEMS];
    int height_px;
    // Must be cached per output in case of different scale factors.
    int cached_text_widths_px[SCRAN_UI_KEYMAP_N_TEXTS];

    uint32_t pressed_items_mask;
};

struct scran_ui_context {
    struct scran_ui_keymap ui_keymap;

    BLContextCore bl_ctx;
    BLFontCore font;

    int ascent_px;
    int font_height_px;
    int fixed_width_font_glyph_width_px;

    bool dirty; // TODO: Track dirty per ui item.
};


bool init_scran_ui_pre_selection(struct scran_ui_context *ui_ctx, double scale);
 void destroy_scran_ui(struct scran_ui_context *ui_ctx);
bool scran_ui_set_selection_stage_defaults( struct scran_ui_context *ui_ctx);
bool reinit_scran_ui(struct scran_ui_context *ui_ctx, double scale);
void redraw_keymap(struct scran_ui_context *ui_ctx);


static inline void
scran_ui_keymap_item_set_color(
    struct scran_ui_context *ui_ctx,
    enum scran_ui_keymap_item_index item_index,
    enum scran_ui_keymap_color color
) {
    struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[item_index];

    keymap_item->live_state.color = color;

    if (!keymap_item->locked) {
        ui_ctx->dirty = true;
    }
}

static inline void
scran_ui_keymap_item_set_text(
    struct scran_ui_context *ui_ctx,
    enum scran_ui_keymap_item_index item_index,
    enum scran_ui_keymap_text text
) {
    struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[item_index];

    keymap_item->live_state.text = text;

    if (!keymap_item->locked) {
        ui_ctx->dirty = true;
    }
}

static inline void
scran_ui_keymap_item_set_pressed(
    struct scran_ui_context *ui_ctx,
    enum scran_ui_keymap_item_index item_index,
    bool pressed
) {
    typeof(ui_ctx->ui_keymap.pressed_items_mask) bit = 1U << item_index;

    if (pressed) {
        ui_ctx->ui_keymap.pressed_items_mask |=  bit;
    } else {
        ui_ctx->ui_keymap.pressed_items_mask &= ~bit;
    }

    ui_ctx->dirty = true;
}

static inline void
scran_ui_keymap_item_set_disabled(
    struct scran_ui_context *ui_ctx,
    enum scran_ui_keymap_item_index item_index,
    enum scran_ui_disable_reason reason,
    bool disabled
) {
    struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[item_index];

    typeof(keymap_item->disable_reason_mask) bit = 1U << reason;

    if (disabled) {
        keymap_item->disable_reason_mask |=  bit;
    } else {
        keymap_item->disable_reason_mask &= ~bit;
    }

    ui_ctx->dirty = true;
}

static inline void
scran_ui_keymap_item_set_locked(
    struct scran_ui_context *ui_ctx,
    enum scran_ui_keymap_item_index item_index,
    bool locked
) {
    struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[item_index];

    if (locked && keymap_item->locked) {
        eprintf("ERROR: Tried to lock already-locked key state. THIS IS A BUG, please open an issue.\n");
    }

    keymap_item->locked_state = keymap_item->live_state;
    keymap_item->locked = locked;

    ui_ctx->dirty = true;
}


#endif
