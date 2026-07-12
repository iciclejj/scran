#ifndef SCRAN_UI_H
#define SCRAN_UI_H


#include <assert.h>
#include <limits.h>

#include <blend2d/blend2d.h>
#include <util/blend2d.h>
#include <util/util.h>

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

enum scran_ui_statusline_item_index {
    SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE,
    SCRAN_UI_STATUSLINE_ITEM_I_TIMER,
    SCRAN_UI_STATUSLINE_N_ITEMS,
};

enum scran_ui_statusline_keymap_item_index {
    SCRAN_UI_STATUSLINE_KEYMAP_ITEM_I_FREEZEFRAME,
    SCRAN_UI_STATUSLINE_KEYMAP_N_ITEMS,
};

enum scran_ui_color {
    SCRAN_UI_COLOR_DEFAULT,
    SCRAN_UI_COLOR_KEYMAP_MOD,
    SCRAN_UI_COLOR_KEYMAP_ALT,
    SCRAN_UI_COLOR_KEYMAP_VIDEO_CAPTURE,
    SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME,
    SCRAN_UI_N_COLORS,
};

enum scran_ui_text {
    SCRAN_UI_TEXT_KEYMAP_EXTRA_PRE_INIT_DEFAULT,

    SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT,
    SCRAN_UI_TEXT_KEYMAP_IMAGE_MOD,

    SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT,
    SCRAN_UI_TEXT_KEYMAP_VIDEO_MOD,

    SCRAN_UI_TEXT_KEYMAP_FOCUS_DEFAULT,
    SCRAN_UI_TEXT_KEYMAP_FOCUS_RELEASED_TRAY,
    SCRAN_UI_TEXT_KEYMAP_FOCUS_RELEASED_HELP,

    SCRAN_UI_TEXT_STATUSLINE_SELECTION_SIZE_DUMMY,
    SCRAN_UI_TEXT_STATUSLINE_TIMER_DUMMY,

    SCRAN_UI_TEXT_STATUSLINE_KEYMAP_FREEZEFRAME_TURN_ON,
    SCRAN_UI_TEXT_STATUSLINE_KEYMAP_FREEZEFRAME_TURN_OFF,

    SCRAN_UI_TEXT_ATLAS_DIGITS,
    SCRAN_UI_TEXT_ATLAS_SEPARATORS,

    SCRAN_UI_TEXT_EMPTY,
    SCRAN_UI_N_TEXTS,
};

struct scran_ui_textline_metadata {
    int height_px;
    uint32_t pressed_items_mask;
    uint32_t dirty_items_mask;
};
struct scran_ui_textline_item_lockable_state {
    uint8_t text;
    uint8_t color;
};
struct scran_ui_textline_item {
    BLImageCore bl_img;
    int width_px; // width of currently displayed text. height_px is shared for entire textline.

    struct scran_ui_textline_item_lockable_state live_state;
    struct scran_ui_textline_item_lockable_state locked_state;

    uint8_t disable_reason_mask;
    bool locked;
};
static_assert(sizeof((struct scran_ui_textline_item){}.disable_reason_mask) * CHAR_BIT >= SCRAN_UI_N_DISABLE_REASONS,
              ".disable_reason_mask must fit all possible disable reasons.");

struct scran_ui_textline_view {
    struct scran_ui_textline_metadata *meta;
    struct scran_ui_textline_item     *items;
    int                                n_items;
};
#define SCRAN_UI_TEXTLINE_VIEW(textline) (          \
    (struct scran_ui_textline_view){                \
        .meta = &(textline).meta,                   \
        .items = (textline).items,                  \
        .n_items = ARRAY_LENGTH((textline).items),  \
    }                                               \
)

struct glyph_atlas {
    struct {
        struct scran_ui_textline_metadata meta;
        struct scran_ui_textline_item     items[1];
    } digits;
    struct {
        struct scran_ui_textline_metadata meta;
        struct scran_ui_textline_item     items[1];
    } separators;
};

struct scran_ui_keymap_textline {
    struct scran_ui_textline_metadata meta;
    struct scran_ui_textline_item     items[SCRAN_UI_KEYMAP_N_ITEMS];
};
struct scran_ui_statusline_textline {
    struct scran_ui_textline_metadata meta;
    struct scran_ui_textline_item     items[SCRAN_UI_STATUSLINE_N_ITEMS];
    int timer_seconds;
    BLRectI selection_size;
};
struct scran_ui_statusline_keymap_textline {
    struct scran_ui_textline_metadata meta;
    struct scran_ui_textline_item     items[SCRAN_UI_STATUSLINE_KEYMAP_N_ITEMS];
};

struct scran_ui_context {
    struct scran_ui_keymap_textline            ui_keymap;
    struct scran_ui_statusline_textline        ui_statusline;
    struct scran_ui_statusline_keymap_textline ui_statusline_keymap;

    struct glyph_atlas glyph_atlas;

    // Must be cached per output in case of different scale factors.
    int cached_text_widths_px[SCRAN_UI_N_TEXTS];

    BLContextCore bl_ctx;
    BLFontCore font;

    float font_ascent;
    float font_height;
    float font_advance_fixed_width;
};


bool init_scran_ui_pre_selection(struct scran_ui_context *ui_ctx, double scale);
 void destroy_scran_ui(struct scran_ui_context *ui_ctx);
bool scran_ui_set_selection_stage_defaults( struct scran_ui_context *ui_ctx);
bool reinit_scran_ui(struct scran_ui_context *ui_ctx, double scale);

enum scran_ui_redrawn_textline_mask {
    SCRAN_UI_REDREW_KEYMAP            = 1U << 0,
    SCRAN_UI_REDREW_STATUSLINE        = 1U << 1,
    SCRAN_UI_REDREW_STATUSLINE_KEYMAP = 1U << 2,
};
// Returns scran_ui_redrawn_textline_mask-valued mask
uint32_t scran_ui_redraw_elements(struct scran_ui_context *ui_ctx);


static inline uint32_t
scran_ui_textline_all_items_mask(
    struct scran_ui_textline_view textline
) {
    assert(0 <= textline.n_items && textline.n_items <= 31);
    return (1U << textline.n_items) - 1U;
}
static inline void
scran_ui_textline_set_all_items_dirty(
    struct scran_ui_textline_view textline
) {
    textline.meta->dirty_items_mask |= scran_ui_textline_all_items_mask(textline);
}
static inline bool
scran_ui_textline_get_items_mask_bit(
    uint32_t mask,
    int item_index
) {
    return mask & (1U << item_index);
}
static inline void
scran_ui_textline_set_items_mask_bit(
    uint32_t *mask,
    int item_index,
    const bool bit_value
) {
    const uint32_t bit = 1U << item_index;
    if (bit_value == 1) {
        *mask |=  bit;
    } else {
        *mask &= ~bit;
    }
}

static inline bool
scran_ui_textline_item_is_dirty(
    struct scran_ui_textline_view textline,
    int item_index
) {
    return scran_ui_textline_get_items_mask_bit(textline.meta->dirty_items_mask, item_index);
}

static inline void
scran_ui_textline_item_set_dirty(
    struct scran_ui_textline_view textline,
    int item_index
) {
    scran_ui_textline_set_items_mask_bit(&textline.meta->dirty_items_mask, item_index, true);
}

static inline bool
scran_ui_textline_item_is_pressed(
    struct scran_ui_textline_view textline,
    int item_index
) {
    return scran_ui_textline_get_items_mask_bit(textline.meta->pressed_items_mask, item_index);
}

static inline void
scran_ui_statusline_set_selection_size(
    struct scran_ui_statusline_textline *statusline,
    BLRectI selection_size
) {
    if (!blrecti_are_equal(statusline->selection_size, selection_size)) {
        statusline->selection_size = selection_size;
        scran_ui_textline_item_set_dirty(SCRAN_UI_TEXTLINE_VIEW(*statusline), SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE);
    }
}

static inline void
scran_ui_statusline_set_timer(
    struct scran_ui_statusline_textline *statusline,
    int total_elapsed_seconds
) {
    if (statusline->timer_seconds != total_elapsed_seconds) {
        statusline->timer_seconds = total_elapsed_seconds;
        scran_ui_textline_item_set_dirty(SCRAN_UI_TEXTLINE_VIEW(*statusline), SCRAN_UI_STATUSLINE_ITEM_I_TIMER);
    }
}

static inline void
scran_ui_textline_item_set_pressed(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    int item_index,
    bool pressed
) {
    scran_ui_textline_set_items_mask_bit(&textline.meta->pressed_items_mask, item_index, pressed);
    scran_ui_textline_item_set_dirty(textline, item_index);
}

static inline void
scran_ui_textline_item_set_color(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    int item_index,
    enum scran_ui_color color
) {
    struct scran_ui_textline_item *item = &textline.items[item_index];

    item->live_state.color = color;

    if (!item->locked) {
        scran_ui_textline_item_set_dirty(textline, item_index);
    }
}

static inline void
scran_ui_textline_item_set_text(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    int item_index,
    enum scran_ui_text text
) {
    struct scran_ui_textline_item *item = &textline.items[item_index];

    item->live_state.text = text;

    if (!item->locked) {
        scran_ui_textline_item_set_dirty(textline, item_index);
    }
}

static inline void
scran_ui_textline_item_set_disabled(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    int item_index,
    enum scran_ui_disable_reason reason,
    bool disabled
) {
    struct scran_ui_textline_item *item = &textline.items[item_index];

    typeof(item->disable_reason_mask) bit = 1U << reason;

    if (disabled) {
        item->disable_reason_mask |=  bit;
    } else {
        item->disable_reason_mask &= ~bit;
    }

    scran_ui_textline_item_set_dirty(textline, item_index);
}

static inline void
scran_ui_textline_item_set_locked(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    int item_index,
    bool locked
) {
    struct scran_ui_textline_item *item = &textline.items[item_index];

    if (locked && item->locked) {
        eprintf("ERROR: Tried to lock already-locked key state. THIS IS A BUG, please open an issue.\n");
    }

    item->locked_state = item->live_state;
    item->locked = locked;

    scran_ui_textline_item_set_dirty(textline, item_index);
}


#endif
