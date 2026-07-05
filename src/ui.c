#include <uchar.h>
#include <assert.h>
#include <stddef.h>

#include <blend2d/blend2d.h>

#include "init.h"
#include "ui.h"
#include "selection-surface.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "util/util.h"


#define SCRAN_SELECTION_SHADOW_OFFSET_PX 1
#define SCRAN_SELECTION_SHADOW_COLOR     ((struct BLRgba32){ 0xDD0E0E0E })

#define CHAR16_STRLEN(s) ( (sizeof(s) / sizeof(char16_t)) - 1)

extern const char scran_font_ttf_start[];
extern const char scran_font_ttf_end[];
static inline size_t get_font_size() {
    return scran_font_ttf_end - scran_font_ttf_start;
}
static inline const void * get_font_data() {
    return scran_font_ttf_start;
}

static const BLRgba32 ui_colors[] = {
    [SCRAN_UI_COLOR_DEFAULT]              = { 0xFFDDDDDD },
    [SCRAN_UI_COLOR_KEYMAP_MOD]           = { 0xFFFFFFAA },
    [SCRAN_UI_COLOR_KEYMAP_ALT]           = { 0xFF888888 },
    [SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME]   = { 0XFF6BE7FF },
    [SCRAN_UI_COLOR_KEYMAP_VIDEO_CAPTURE] = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE,
};
static_assert(sizeof(ui_colors) / sizeof(ui_colors[0]) == SCRAN_UI_N_COLORS,
              "ui_colors[] length must exactly cover all color enum values.");

struct ui_string {
    const char16_t *str;
    const size_t strlen;
};
#define INIT_UI_STRING(s) { .str = (s), .strlen = CHAR16_STRLEN(s) }

static const struct ui_string ui_texts[] = {
    // XXX: Leading space so it doesn't hug the edge when drawn at x=0,y=0.
    //      The top margin is fine already.
    //      Somewhat of a HACK, but shouldn't cause any issues unless we change
    //      the font or origin point.
    [SCRAN_UI_TEXT_KEYMAP_EXTRA_PRE_INIT_DEFAULT]          = INIT_UI_STRING(u" Click and drag to make selection"),

    [SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT]                   = INIT_UI_STRING(u"[↵] Image & Exit"),
    [SCRAN_UI_TEXT_KEYMAP_IMAGE_MOD]                       = INIT_UI_STRING(u"[↵] Image       "),

    [SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT]                   = INIT_UI_STRING(u"[␣] Video \uf028"),
    [SCRAN_UI_TEXT_KEYMAP_VIDEO_MOD]                       = INIT_UI_STRING(u"[␣] Video \uf026"),

    [SCRAN_UI_TEXT_KEYMAP_FOCUS_DEFAULT]                   = INIT_UI_STRING(u"[⇥] Release focus"),
    [SCRAN_UI_TEXT_KEYMAP_FOCUS_RELEASED_TRAY]             = INIT_UI_STRING(u"[⇥] Click tray icon to retake focus."),
    [SCRAN_UI_TEXT_KEYMAP_FOCUS_RELEASED_HELP]             = INIT_UI_STRING(u"[⇥] Focus released. 'scran -h' for help."),

    [SCRAN_UI_TEXT_STATUSLINE_KEYMAP_FREEZEFRAME_TURN_ON]  = INIT_UI_STRING(u"[Z] Freeze screen"),
    [SCRAN_UI_TEXT_STATUSLINE_KEYMAP_FREEZEFRAME_TURN_OFF] = INIT_UI_STRING(u"[Z] Unfreeze screen"),

    [SCRAN_UI_TEXT_EMPTY]                                  = INIT_UI_STRING(u""),
};
static_assert(sizeof(ui_texts) / sizeof(ui_texts[0]) == SCRAN_UI_N_TEXTS,
              "ui_texts[] length must exactly cover all text enum values.");


static inline void
redraw_textline_item_image(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_item *item,
    bool pressed
) {
    const struct scran_ui_textline_item_lockable_state lockable_state =
        item->locked ? item->locked_state : item->live_state;

    const struct ui_string *string = &ui_texts[lockable_state.text];

    bl_context_begin(&ui_ctx->bl_ctx, &item->bl_img, NULL);

    BLPointI origin = {
        .x = 0,
        .y = ui_ctx->ascent_px,
    };
    BLPointI origin_shadow = {
        .x = 0                 + SCRAN_SELECTION_SHADOW_OFFSET_PX,
        .y = ui_ctx->ascent_px + SCRAN_SELECTION_SHADOW_OFFSET_PX,
    };

    BLRgba32 color = ui_colors[lockable_state.color];
    bl_context_clear_all(&ui_ctx->bl_ctx);

    if (item->disable_reason_mask != 0U) {
        BLRgba32 _color = color;
        blrgba32_scale_colors(&_color, 0.64f); // dim the color
        bl_context_fill_utf16_text_i_rgba32(
            &ui_ctx->bl_ctx, &origin_shadow, &ui_ctx->font, string->str, string->strlen, SCRAN_SELECTION_SHADOW_COLOR.value
        );
        bl_context_fill_utf16_text_i_rgba32(
            &ui_ctx->bl_ctx, &origin       , &ui_ctx->font, string->str, string->strlen, _color.value
        );
    } else if (pressed) {
        bl_context_fill_utf16_text_i_rgba32(
            &ui_ctx->bl_ctx, &origin_shadow, &ui_ctx->font, string->str, string->strlen, color.value
        );
    } else {
        bl_context_fill_utf16_text_i_rgba32(
            &ui_ctx->bl_ctx, &origin_shadow, &ui_ctx->font, string->str, string->strlen, SCRAN_SELECTION_SHADOW_COLOR.value
        );
        bl_context_fill_utf16_text_i_rgba32(
            &ui_ctx->bl_ctx, &origin       , &ui_ctx->font, string->str, string->strlen, color.value
        );
    }

    bl_context_end(&ui_ctx->bl_ctx);

    item->width_px = ui_ctx->cached_text_widths_px[lockable_state.text];
}


static inline bool
redraw_textline(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline
) {
    bool redrew = false;

    for (int i = 0; i < textline.n_items; ++i) {
        bool item_dirty = scran_ui_textline_get_items_mask_bit(textline.meta->dirty_items_mask, i);
        if (item_dirty) {
            bool pressed = scran_ui_textline_get_items_mask_bit(textline.meta->pressed_items_mask, i);
            redraw_textline_item_image(ui_ctx, &textline.items[i], pressed);
            redrew |= item_dirty;
        }
    }
    textline.meta->dirty_items_mask = 0;

    return redrew;
}

uint32_t
scran_ui_redraw_elements(
    struct scran_ui_context *ui_ctx
) {
    uint32_t redrawn_textline_mask = 0;

    if (redraw_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap))) {
        redrawn_textline_mask |= SCRAN_UI_REDREW_KEYMAP;
    }
    if (redraw_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline))) {
        redrawn_textline_mask |= SCRAN_UI_REDREW_STATUSLINE;
    }
    if (redraw_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline_keymap))) {
        redrawn_textline_mask |= SCRAN_UI_REDREW_STATUSLINE_KEYMAP;
    }

    return redrawn_textline_mask;
}

static inline struct BLTextMetrics
get_bl_text_metrics(
    BLFontCore *font,
    const char16_t *text,
    size_t text_strlen
) {
    BLGlyphBufferCore glyph_buffer;
    bl_glyph_buffer_init(&glyph_buffer);
    bl_glyph_buffer_set_text(&glyph_buffer, text, text_strlen, BL_TEXT_ENCODING_UTF16);

    BLTextMetrics text_metrics;
    bl_font_get_text_metrics(font, &glyph_buffer, &text_metrics);

    bl_glyph_buffer_destroy(&glyph_buffer);

    return text_metrics;
}

static inline int
calculate_bl_text_width_px(
    BLFontCore *font,
    const char16_t *text,
    size_t text_strlen
) {
    BLTextMetrics text_metrics = get_bl_text_metrics(font, text, text_strlen);
    double        width        = text_metrics.advance.x;
    int           width_px     = ceil(width) + (!!width * SCRAN_SELECTION_SHADOW_OFFSET_PX);

    return width_px;
}

static inline void
reinit_textline(
    struct scran_ui_textline_view textline,
    int w_px, int h_px
) {
    for (int i = 0; i < textline.n_items; ++i) {
        struct scran_ui_textline_item *item = &textline.items[i];
        bl_image_reset(&item->bl_img);
        bl_image_create(&item->bl_img, w_px, h_px, wl_shm_format_to_blend2d(SURFACE_SHM_FORMAT));
    }
    scran_ui_textline_set_all_items_dirty(textline);

    textline.meta->height_px = h_px;
}

// Should be called on scale changes to resize fonts etc.
bool
reinit_scran_ui(
    struct scran_ui_context *ui_ctx,
    double scale
) {
    BLFontCore *font = &ui_ctx->font;

    if (scale == 0) {
        return false;
    }

    bl_font_reset(font);
    {
        BLFontDataCore font_data;
        bl_font_data_init(&font_data);
        bl_font_data_create_from_data(&font_data, get_font_data(), get_font_size(), NULL, NULL);

        BLFontFaceCore font_face;
        bl_font_face_init(&font_face);
        bl_font_face_create_from_data(&font_face, &font_data, 0);

        const float font_size = scale * 15.f;
        bl_font_init(font);
        bl_font_create_from_face(font, &font_face, font_size);

        bl_font_data_destroy(&font_data);
        bl_font_face_destroy(&font_face);
    }

    int ascent_px;
    int font_height_px;
    {
        BLFontMetrics font_metrics;
        bl_font_get_metrics(font, &font_metrics);

        int descent_px = ceil(font_metrics.descent);
        ascent_px      = ceil(font_metrics.ascent);
        font_height_px = ascent_px + descent_px + SCRAN_SELECTION_SHADOW_OFFSET_PX;
    }

    int fixed_width_font_glyph_width_px;
    {
        static const char16_t single_glyph[] = u"W";
        BLTextMetrics text_metrics = get_bl_text_metrics(font, single_glyph, CHAR16_STRLEN(single_glyph));
        fixed_width_font_glyph_width_px = ceil(text_metrics.advance.x);
    }

    ui_ctx->ascent_px = ascent_px;
    ui_ctx->font_height_px = font_height_px;
    ui_ctx->fixed_width_font_glyph_width_px = fixed_width_font_glyph_width_px;

    // - Allocate a buffer that fits the largest possible string for all text images
    // - Pre-calculate the pixel-widths of each text
    {
        int width_px_max   = 0;

        // Some of this could be done at compile-time, but would require some ugly macros...
        for (enum scran_ui_text i = 0; i < SCRAN_UI_N_TEXTS; ++i) {
            const struct ui_string *string = &ui_texts[i];
            int width_px = calculate_bl_text_width_px(font, string->str, string->strlen);
            if (width_px_max < width_px) {
                width_px_max = width_px;
            }
            ui_ctx->cached_text_widths_px[i] = width_px;
        }

        assert(width_px_max != 0);

        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap),            width_px_max, font_height_px);
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline),        width_px_max, font_height_px);
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline_keymap), width_px_max, font_height_px);
    }

    scran_ui_redraw_elements(ui_ctx);

    return true;
}

struct default_textline_values {
    enum scran_ui_text  text;
    enum scran_ui_color color;
};
static const struct default_textline_values m_statusline_keymap_defaults_pre_selection[] = {
    [SCRAN_UI_STATUSLINE_KEYMAP_ITEM_I_FREEZEFRAME] = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_statusline_defaults_pre_selection[] = {
    [SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE]     = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_STATUSLINE_ITEM_I_TIMER]              = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_keymap_defaults_pre_selection[] = {
    [SCRAN_UI_KEYMAP_ITEM_I_IMAGE]                  = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_VIDEO]                  = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FOCUS]                  = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
    // We use the pre-selection keymap textline to draw the splash text.
    [SCRAN_UI_KEYMAP_ITEM_I_EXTRA]                  = { SCRAN_UI_TEXT_KEYMAP_EXTRA_PRE_INIT_DEFAULT, SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_keymap_defaults_post_selection[] = {
    [SCRAN_UI_KEYMAP_ITEM_I_IMAGE]                  = { SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT,          SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_VIDEO]                  = { SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT,          SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FOCUS]                  = { SCRAN_UI_TEXT_KEYMAP_FOCUS_DEFAULT,          SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_EXTRA]                  = { SCRAN_UI_TEXT_EMPTY,                         SCRAN_UI_COLOR_DEFAULT },
};
static_assert(ARRAY_LENGTH(m_statusline_keymap_defaults_pre_selection) == SCRAN_UI_STATUSLINE_KEYMAP_N_ITEMS, "");
static_assert(ARRAY_LENGTH(m_statusline_defaults_pre_selection)        == SCRAN_UI_STATUSLINE_N_ITEMS,        "");
static_assert(ARRAY_LENGTH(m_keymap_defaults_pre_selection)            == SCRAN_UI_KEYMAP_N_ITEMS,            "");
static_assert(ARRAY_LENGTH(m_keymap_defaults_post_selection)           == SCRAN_UI_KEYMAP_N_ITEMS,            "");

static inline void
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
        scran_ui_textline_item_set_dirty(textline, i);
    }
}

static inline void
init_textline(
    struct scran_ui_textline_view textline,
    const struct default_textline_values *defaults,
    int n_defaults
) {
    assert(textline.n_items == n_defaults);

    for (int i = 0; i < textline.n_items; ++i) {
        struct scran_ui_textline_item *item = &textline.items[i];
        bl_image_init(&item->bl_img);
    }
    assign_textline_defaults(textline, defaults, n_defaults);
}

bool
init_scran_ui_pre_selection(
    struct scran_ui_context *ui_ctx,
    double scale
) {
    bl_font_init(&ui_ctx->font);
    bl_context_init(&ui_ctx->bl_ctx);

    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap),            m_keymap_defaults_pre_selection,            ARRAY_LENGTH(m_keymap_defaults_pre_selection));
    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline),        m_statusline_defaults_pre_selection,        ARRAY_LENGTH(m_statusline_defaults_pre_selection));
    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline_keymap), m_statusline_keymap_defaults_pre_selection, ARRAY_LENGTH(m_statusline_keymap_defaults_pre_selection));

    reinit_scran_ui(ui_ctx, scale);

    return true;
}

bool
scran_ui_set_selection_stage_defaults(
    struct scran_ui_context *ui_ctx
) {
    assign_textline_defaults(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), m_keymap_defaults_post_selection, ARRAY_LENGTH(m_keymap_defaults_post_selection));
    return true;
}

static inline void
destroy_textline(
    struct scran_ui_textline_view textline
) {
    for (int i = 0; i < textline.n_items; ++i) {
        bl_image_destroy(&textline.items[i].bl_img);
    }
}

void
destroy_scran_ui(
    struct scran_ui_context *ui_ctx
) {
    bl_font_destroy(&ui_ctx->font);
    bl_context_destroy(&ui_ctx->bl_ctx);

    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap));
    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline));
    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline_keymap));
}
