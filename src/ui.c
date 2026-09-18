#include <uchar.h>
#include <assert.h>
#include <stddef.h>

#include <blend2d/blend2d.h>

#include "scran-ui-text.h"
#include "ui.h"
#include "selection-surface.h"
#include "util/util.h"
// XXX XXX TODO: Move ui.c into this folder too, probably
#include "./ui/atlas.h"


#define SCRAN_SELECTION_SHADOW_COLOR     ((struct BLRgba32){ 0xDD0E0E0E })

static const BLRgba32 ui_colors[] = {
    [SCRAN_UI_COLOR_DEFAULT]              = { 0xFFDDDDDD },
    [SCRAN_UI_COLOR_KEYMAP_MOD]           = { 0xFFFFFFAA },
    [SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME]   = { 0XFF6BE7FF },
    [SCRAN_UI_COLOR_KEYMAP_VIDEO_CAPTURE] = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE,
};
static_assert(sizeof(ui_colors) / sizeof(ui_colors[0]) == SCRAN_UI_N_COLORS,
              "ui_colors[] length must exactly cover all color enum values.");

static inline struct atlas_text_metrics
get_item_spacing(struct scran_ui_context *ui_ctx) {
    return (struct atlas_text_metrics) {
        .advance.x = round(3 * ui_ctx->glyph_atlas_2.fallback_glyph_advance.x),
    };
}

static inline void
fill_char16(char16_t *str, int n, char16_t char_) {
    for (int i = 0; i < n; ++i) {
        str[i] = char_;
    }
}

// Returns final cursor location
static char16_t *
append_char16_uint(char16_t *cursor, char16_t *right_bound, uint32_t uint_)
{
    char16_t *const start = cursor;

    // Generate the number in reverse
    while (cursor < right_bound) {
        *(cursor++) = u'0' + uint_ % 10;
        uint_ /= 10;

        if (!uint_) {
            break;
        }
    }

    char16_t *const end = cursor;

    // Reverse it
    char16_t *left  = start;
    char16_t *right = end - 1;
    while (right - left >= 1) {
        char16_t tmp = *left;
        *left++  = *right;
        *right-- = tmp;
    }

    return end;
}

static inline char16_t *
prepend_char16_uint_two_digits(char16_t *start, uint32_t uint_) {
    *(--start) = u'0' + uint_ % 10;
    *(--start) = u'0' + uint_ / 10;
    return start;
}

static const size_t TIMER_STRLEN          = CHAR16_STRLEN(g_ui_strings.statusline_timer_dummy);
static const size_t SELECTION_SIZE_STRLEN = CHAR16_STRLEN(g_ui_strings.statusline_selection_size_dummy);

static void
get_timer_string(
    char16_t string_char16[static TIMER_STRLEN],
    int seconds
) {
    fill_char16(string_char16, TIMER_STRLEN, u' ');
    char16_t *cursor = string_char16 + TIMER_STRLEN;

    int hours_   =  seconds / 3600;
    int minutes_ = (seconds / 60) % 60;
    int seconds_ =  seconds % 60;

    // XXX: The image buffer is fixed-size, allowing max 2 digits per time unit.
    // We let seconds keep ticking, to show some signs of life.
    if (hours_ > 99) {
        hours_   = 99;
        minutes_ = 59;
    }

    assert(seconds_ >= 0); // XXX TODO: Ensure this better or handle it here?
    cursor = prepend_char16_uint_two_digits(cursor, seconds_);
    *(--cursor) = ':';
    cursor = prepend_char16_uint_two_digits(cursor, minutes_);

    if (hours_) {
        *(--cursor) = ':';
        cursor = prepend_char16_uint_two_digits(cursor, hours_);
    }
}

static void
get_selection_size_string(
    char16_t string_char16[static SELECTION_SIZE_STRLEN],
    BLRectI size
) {
    fill_char16(string_char16, SELECTION_SIZE_STRLEN, u' ');

    char16_t *cursor      = string_char16;
    char16_t *right_bound = string_char16 + SELECTION_SIZE_STRLEN;

    // We want it left-aligned to show nicely on the pre-selection screen
    cursor = append_char16_uint(cursor, right_bound, abs(size.w)); // Leftmost value
    *(cursor++) = u'x';
    cursor = append_char16_uint(cursor, right_bound, abs(size.h)); // Rightmost value
}

struct text_blit_data {
    const struct ui_string *text;
};

static struct atlas_text_metrics
blit_text(
    const struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx_destination,
    const BLPointI *origin,
    const struct text_blit_data *data
) {
    BLPointI glyph_origin = *origin;
    struct atlas_text_metrics text_metrics = {0};

    for (size_t i_glyph = 0; i_glyph < data->text->strlen; ++i_glyph) {
        const struct atlas_text_metrics glyph_metrics = scran_ui_atlas_blit_glyph(
            &ui_ctx->glyph_atlas_2,
            bl_ctx_destination,
            &glyph_origin,
            data->text->str[i_glyph]
        );

        scran_atlas_append_text_metrics(&text_metrics, &glyph_metrics);
        glyph_origin.x = origin->x + atlas_metrics_pen_x_px(&text_metrics);
    }

    return text_metrics;
}

static struct atlas_text_metrics
blit_static_textline(
    struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx,
    const BLPointI *origin,
    struct scran_ui_textline_view *textline
) {
    struct atlas_text_metrics textline_metrics = {0};

    for (int i_item = 0; i_item < textline->n_items; ++i_item) {
        const struct scran_ui_textline_item *item = &textline->items[i_item];
        const struct scran_ui_textline_item_lockable_state *state = item->locked ? &item->locked_state : &item->live_state;

        if (i_item > 0) {
            struct atlas_text_metrics item_spacing = get_item_spacing(ui_ctx);
            scran_atlas_append_text_metrics(&textline_metrics, &item_spacing);
        }
        BLPointI item_origin = {
            .x = origin->x + atlas_metrics_pen_x_px(&textline_metrics),
            .y = origin->y,
        };
        const struct text_blit_data blit_data = {
            .text = &state->text,
        };
        const struct atlas_text_metrics item_metrics = blit_text(ui_ctx, bl_ctx, &item_origin, &blit_data);

        scran_atlas_append_text_metrics(&textline_metrics, &item_metrics);
    }

    return textline_metrics;
}

struct atlas_text_metrics
scran_ui_blit_greeting(
    struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx,
    const BLPointI *origin
) {
    return blit_static_textline(ui_ctx, bl_ctx, origin, &SCRAN_UI_TEXTLINE(ui_ctx->ui_greeting));
}

struct atlas_text_metrics
scran_ui_blit_keymap(
    struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx,
    const BLPointI *origin
) {
    return blit_static_textline(ui_ctx, bl_ctx, origin, &SCRAN_UI_TEXTLINE(ui_ctx->ui_keymap));
}

struct atlas_text_metrics
scran_ui_blit_statusline(
    struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx_destination,
    const BLPointI *origin
) {
    struct scran_ui_statusline_textline *statusline = &ui_ctx->ui_statusline;
    struct atlas_text_metrics textline_metrics = {0};

    const struct atlas_text_metrics item_spacing = get_item_spacing(ui_ctx);

    {
        char16_t text[SELECTION_SIZE_STRLEN];
        get_selection_size_string(text, statusline->selection_size);

        struct text_blit_data blit_data = {
            .text = &(struct ui_string) {
                .str = (const char16_t *)&text, // XXX: Fix this cast?
                .strlen = SELECTION_SIZE_STRLEN,
            }
        };
        BLPointI item_origin = {
            .x = origin->x + atlas_metrics_pen_x_px(&textline_metrics),
            .y = origin->y,
        };
        struct atlas_text_metrics item_metrics = blit_text(
            ui_ctx,
            bl_ctx_destination,
            &item_origin,
            &blit_data
        );
        scran_atlas_append_text_metrics(&textline_metrics, &item_metrics);
    }

    scran_atlas_append_text_metrics(&textline_metrics, &item_spacing);

    {
        char16_t timer_string[TIMER_STRLEN];
        get_timer_string(timer_string, statusline->timer_seconds);

        const struct text_blit_data blit_data = {
            .text = &(struct ui_string) {
                .str = (const char16_t *)&timer_string, // XXX: Fix this cast?
                .strlen = TIMER_STRLEN,
            },
        };
        BLPointI item_origin = {
            .x = origin->x + atlas_metrics_pen_x_px(&textline_metrics),
            .y = origin->y,
        };
        struct atlas_text_metrics item_metrics = blit_text(
            ui_ctx,
            bl_ctx_destination,
            &item_origin,
            &blit_data
        );
        scran_atlas_append_text_metrics(&textline_metrics, &item_metrics);
    }

    return textline_metrics;
}


struct atlas_text_metrics
scran_ui_compute_statusline_metrics_px(
    struct scran_ui_context *ui_ctx
) {
    struct scran_ui_statusline_textline *statusline = &ui_ctx->ui_statusline;
    struct atlas_text_metrics textline_metrics = {0};

    {
        char16_t selection_size_string[SELECTION_SIZE_STRLEN];
        get_selection_size_string(selection_size_string, statusline->selection_size);

        const struct atlas_text_metrics item_metrics = scran_atlas_get_text_metrics_px(
            &ui_ctx->glyph_atlas_2,
            selection_size_string,
            ARRAY_LENGTH(selection_size_string)
        );
        scran_atlas_append_text_metrics(&textline_metrics, &item_metrics);
    }

    {
        const struct atlas_text_metrics item_spacing = get_item_spacing(ui_ctx);
        scran_atlas_append_text_metrics(&textline_metrics, &item_spacing);
    }

    {
        char16_t timer_string[TIMER_STRLEN];
        get_timer_string(timer_string, statusline->timer_seconds);

        const struct atlas_text_metrics item_metrics = scran_atlas_get_text_metrics_px(
            &ui_ctx->glyph_atlas_2,
            timer_string,
            ARRAY_LENGTH(timer_string)
        );
        scran_atlas_append_text_metrics(&textline_metrics, &item_metrics);
    }

    return textline_metrics;
}


struct atlas_text_metrics
scran_ui_compute_textline_metrics_px(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view *textline
) {
    struct atlas_text_metrics textline_metrics = {0};

    for (int i = 0; i < textline->n_items; ++i) {
        const struct scran_ui_textline_item *item = &textline->items[i];
        const struct scran_ui_textline_item_lockable_state *state = item->locked
            ? &item->locked_state
            : &item->live_state;
        const struct ui_string *text = &state->text;

        if (i > 0) {
            struct atlas_text_metrics item_spacing = get_item_spacing(ui_ctx);
            scran_atlas_append_text_metrics(&textline_metrics, &item_spacing);
        }
        const struct atlas_text_metrics item_metrics = scran_atlas_get_text_metrics_px(
            &ui_ctx->glyph_atlas_2,
            text->str,
            text->strlen
        );
        scran_atlas_append_text_metrics(&textline_metrics, &item_metrics);
    }

    return textline_metrics;
}

struct default_textline_values {
    struct ui_string    text;
    enum scran_ui_color color;
};
static const struct default_textline_values m_greeting_defaults[] = {
    [SCRAN_UI_GREETING_ITEM_I_GREETING]             = { UI_STRING(g_ui_strings.greeting),                        SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_statusline_defaults[] = {
    [SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE]     = { UI_STRING(g_ui_strings.statusline_selection_size_dummy), SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_STATUSLINE_ITEM_I_TIMER]              = { UI_STRING(g_ui_strings.statusline_timer_dummy),          SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_keymap_defaults[] = {
    [SCRAN_UI_KEYMAP_ITEM_I_IMAGE]                  = { UI_STRING(g_ui_strings.keymap_image_default),            SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_VIDEO]                  = { UI_STRING(g_ui_strings.keymap_video_default),            SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME]            = { UI_STRING(g_ui_strings.keymap_freezeframe_turn_on),      SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FOCUS]                  = { UI_STRING(g_ui_strings.keymap_focus_default),            SCRAN_UI_COLOR_DEFAULT },
};
static_assert(ARRAY_LENGTH(m_greeting_defaults)          == SCRAN_UI_GREETING_N_ITEMS,                             "");
static_assert(ARRAY_LENGTH(m_statusline_defaults)        == SCRAN_UI_STATUSLINE_N_ITEMS,                           "");
static_assert(ARRAY_LENGTH(m_keymap_defaults)            == SCRAN_UI_KEYMAP_N_ITEMS,                               "");

static void
assign_textline_defaults(
    struct scran_ui_textline_view textline,
    const struct default_textline_values *defaults,
    int n_defaults
) {
    assert(textline.n_items == n_defaults);

    for (int i = 0; i < textline.n_items; ++i) {
        struct scran_ui_textline_item *item = &textline.items[i];
        assert(item->locked == false);
        item->live_state.text  = defaults[i].text;
        item->live_state.color = defaults[i].color;
        scran_ui_textline_set_dirty(textline);
    }
}

static void
init_textline(
    struct scran_ui_textline_view textline,
    const struct default_textline_values *defaults,
    int n_defaults
) {
    assert(textline.n_items == n_defaults);
    assign_textline_defaults(textline, defaults, n_defaults);
}

bool
init_scran_ui_pre_selection(
    struct scran_ui_context *ui_ctx,
    double scale
) {
    init_textline(SCRAN_UI_TEXTLINE(ui_ctx->ui_greeting),           m_greeting_defaults,          ARRAY_LENGTH(m_greeting_defaults));
    init_textline(SCRAN_UI_TEXTLINE(ui_ctx->ui_keymap),             m_keymap_defaults,            ARRAY_LENGTH(m_keymap_defaults));
    init_textline(SCRAN_UI_TEXTLINE(ui_ctx->ui_statusline),         m_statusline_defaults,        ARRAY_LENGTH(m_statusline_defaults));

    for (int i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        scran_ui_textline_item_set_disabled(SCRAN_UI_TEXTLINE(ui_ctx->ui_keymap), i, SCRAN_UI_DISABLE_REASON_NOT_ACTIVE_SURFACE, true);
    }

    scran_ui_init_atlas(ui_ctx, scale);

    return true;
}

void
destroy_scran_ui(
    struct scran_ui_context *ui_ctx
) {
    scran_ui_destroy_atlas(&ui_ctx->glyph_atlas_2);
}
