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
    [SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME]   = { 0XFF6BE7FF },
    [SCRAN_UI_COLOR_KEYMAP_VIDEO_CAPTURE] = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE,
};
static_assert(sizeof(ui_colors) / sizeof(ui_colors[0]) == SCRAN_UI_N_COLORS,
              "ui_colors[] length must exactly cover all color enum values.");

struct ui_string {
    const char16_t *str;
    const size_t strlen;
};
#define INIT_UI_STRING(s) ((struct ui_string){ .str = (s), .strlen = CHAR16_STRLEN(s) })

static const struct ui_string ui_texts[] = {
    [SCRAN_UI_TEXT_GREETING]                        = INIT_UI_STRING(u"Click and drag anywhere to select a custom region"),

    [SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT]            = INIT_UI_STRING(u"[↵] Image & Exit"),
    [SCRAN_UI_TEXT_KEYMAP_IMAGE_MOD]                = INIT_UI_STRING(u"[↵] Image       "),

    [SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT]            = INIT_UI_STRING(u"[␣] Video \uf028"),
    [SCRAN_UI_TEXT_KEYMAP_VIDEO_MOD]                = INIT_UI_STRING(u"[␣] Video \uf026"),

    [SCRAN_UI_TEXT_KEYMAP_FOCUS_DEFAULT]            = INIT_UI_STRING(u"[⇥] Release focus"),
    [SCRAN_UI_TEXT_KEYMAP_FOCUS_RELEASED_TRAY]      = INIT_UI_STRING(u"[⇥] Click tray icon to retake focus."),
    [SCRAN_UI_TEXT_KEYMAP_FOCUS_RELEASED_HELP]      = INIT_UI_STRING(u"[⇥] Focus released. 'scran -h' for help."),

    [SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_ON]      = INIT_UI_STRING(u"[Z] Freeze screens"),
    [SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_OFF]     = INIT_UI_STRING(u"[Z] Unfreeze screens"),

    // Placeholders for calculating metadata (currently just max pixel widths)
    // - actual text is dynamic for these.
    [SCRAN_UI_TEXT_STATUSLINE_SELECTION_SIZE_DUMMY] = INIT_UI_STRING(u"WWWWWxHHHHH"),
    [SCRAN_UI_TEXT_STATUSLINE_TIMER_DUMMY]          = INIT_UI_STRING(u"00:00:00"),

    [SCRAN_UI_TEXT_ATLAS_DIGITS]                    = INIT_UI_STRING(u"0123456789"),
    [SCRAN_UI_TEXT_ATLAS_SEPARATORS]                = INIT_UI_STRING(u":x"),

    [SCRAN_UI_TEXT_EMPTY]                           = INIT_UI_STRING(u""),
};
static_assert(sizeof(ui_texts) / sizeof(ui_texts[0]) == SCRAN_UI_N_TEXTS,
              "ui_texts[] length must exactly cover all text enum values.");

static inline void
redraw_textline_item_image_impl(
    struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx,
    struct scran_ui_textline_item *item,
    const struct ui_string *string,
    const BLPointI origin,
    const BLRgba32 color,
    bool pressed
) {
    BLPointI origin_shadow = {
        .x = origin.x + SCRAN_SELECTION_SHADOW_OFFSET_PX,
        .y = origin.y + SCRAN_SELECTION_SHADOW_OFFSET_PX,
    };

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
}

static inline void
redraw_textline_item_image(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_item *item,
    struct scran_ui_textline_item_lockable_state lockable_state,
    const struct ui_string *string,
    bool pressed
) {
    BLPointI origin = {
        .x = 0,
        .y = round(ui_ctx->font_ascent),
    };
    BLRgba32 color = ui_colors[lockable_state.color];

    bl_context_begin(&ui_ctx->bl_ctx, &item->bl_img, NULL);
    bl_context_clear_all(&ui_ctx->bl_ctx);

    redraw_textline_item_image_impl(ui_ctx, &ui_ctx->bl_ctx, item, string, origin, color, pressed);

    bl_context_end(&ui_ctx->bl_ctx);
    item->width_px = ui_ctx->cached_text_widths_px[lockable_state.text];
}

// Need to make more space than just the advance for the atlas image itself, to ensure adjecent glyphs don't overlap the shadow
static inline int
get_glyph_atlas_advance_px(struct scran_ui_context *ui_ctx) {
    return ceil(ui_ctx->font_advance_fixed_width) + SCRAN_SELECTION_SHADOW_OFFSET_PX;
}
// We manually enforce exact-pixel advances in the blit destination as well, to
// prevent resampling or imperfect slice offsets.
static inline int
get_glyph_atlas_dst_advance_px(struct scran_ui_context *ui_ctx) {
    return ceil(ui_ctx->font_advance_fixed_width);
}

static inline void
blit_atlas_glyph(
    struct scran_ui_context *ui_ctx,
    BLContextCore *bl_ctx_destination,
    char glyph,
    BLPointI *origin
) {
    BLImageCore *bl_img_atlas;
    int atlas_glyph_i = 0;

    switch (glyph) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            bl_img_atlas       = &ui_ctx->glyph_atlas.digits.items[0].bl_img;
            atlas_glyph_i      = glyph - '0';
            break;

        // These could be made more clever if we'll have more options in the future.
        case ':':
            bl_img_atlas       = &ui_ctx->glyph_atlas.separators.items[0].bl_img;
            atlas_glyph_i      = 0;
            break;
        case 'x':
            bl_img_atlas       = &ui_ctx->glyph_atlas.separators.items[0].bl_img;
            atlas_glyph_i      = 1;
            break;

        default:
            assert(false && "Unexpected atlas glyph");
            return;
    };

    int const advance_px = get_glyph_atlas_advance_px(ui_ctx);
    bl_context_blit_image_i(
        bl_ctx_destination,
        origin,
        bl_img_atlas,
        &(BLRectI){
            .x = atlas_glyph_i * advance_px,
            .y = 0,
            .w = advance_px,
            .h = scran_ui_font_height_px(ui_ctx),
        }
    );
};

// TODO: Better name for this function, or combine with the old
// redraw_textline_item_image() function?
static inline void
redraw_textline_item_image_using_atlas(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    enum scran_ui_statusline_item_index i,
    const struct ui_string *ui_string
) {
    struct scran_ui_textline_item *item = &textline.items[i];
    int const advance_px = get_glyph_atlas_dst_advance_px(ui_ctx);
    int       pen_position_px = 0;

    bl_context_begin(&ui_ctx->bl_ctx, &item->bl_img, NULL);
    bl_context_clear_all(&ui_ctx->bl_ctx);

    for (size_t i = 0; i < ui_string->strlen; ++i) {
        char16_t glyph = ui_string->str[i];

        if (glyph != u' ') {
            blit_atlas_glyph(
                ui_ctx,
                &ui_ctx->bl_ctx,
                glyph,
                &(BLPointI){
                    .x = pen_position_px,
                    .y = 0,
                }
            );
        }

        pen_position_px += advance_px;
    }

    bl_context_end(&ui_ctx->bl_ctx);

    item->width_px = pen_position_px == 0
                   ? 0
                   : pen_position_px + SCRAN_SELECTION_SHADOW_OFFSET_PX;
}

static inline void
redraw_glyph_atlas_item_image(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_item *item,
    struct scran_ui_textline_item_lockable_state lockable_state,
    const struct ui_string *string,
    bool pressed
) {
    const int cell_width_px = get_glyph_atlas_advance_px(ui_ctx);
    BLRgba32 color = ui_colors[lockable_state.color];

    bl_context_begin(&ui_ctx->bl_ctx, &item->bl_img, NULL);
    bl_context_clear_all(&ui_ctx->bl_ctx);

    BLPointI origin_current = {
        .x = 0,
        .y = round(ui_ctx->font_ascent),
    };
    struct ui_string string_current = {
        .str = string->str,
        .strlen = 1,
    };

    for (size_t i = 0; i < string->strlen; ++i) {
        redraw_textline_item_image_impl(ui_ctx, &ui_ctx->bl_ctx, item, &string_current, origin_current, color, pressed);
        origin_current.x   += cell_width_px;
        string_current.str += 1;
    }

    bl_context_end(&ui_ctx->bl_ctx);

    item->width_px = string->strlen * cell_width_px;
}

static inline void
redraw_glyph_atlas_textline(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline
) {
    for (int i = 0; i < textline.n_items; ++i) {
        if (scran_ui_textline_item_is_dirty(textline, i)) {
            struct scran_ui_textline_item                *item      = &textline.items[i];
            struct scran_ui_textline_item_lockable_state  state     = item->locked ? item->locked_state : item->live_state;
            bool                                          pressed   = scran_ui_textline_item_is_pressed(textline, i);
            const struct ui_string                       *ui_string = &ui_texts[state.text];
            redraw_glyph_atlas_item_image(ui_ctx, item, state, ui_string, pressed);
        }
    }
    textline.meta->dirty_items_mask = 0;
}

static inline bool
redraw_static_textline(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline
) {
    bool redrew = false;

    for (int i = 0; i < textline.n_items; ++i) {
        if (scran_ui_textline_item_is_dirty(textline, i)) {
            struct scran_ui_textline_item                *item      = &textline.items[i];
            struct scran_ui_textline_item_lockable_state  state     = item->locked ? item->locked_state : item->live_state;
            bool                                          pressed   = scran_ui_textline_item_is_pressed(textline, i);
            const struct ui_string                       *ui_string = &ui_texts[state.text];

            redraw_textline_item_image(ui_ctx, item, state, ui_string, pressed);

            redrew = true;
        }
    }
    textline.meta->dirty_items_mask = 0;

    return redrew;
}

static inline void
fill_char16(char16_t *str, int n, char16_t char_) {
    for (int i = 0; i < n; ++i) {
        str[i] = char_;
    }
}

// Returns final cursor location
static inline char16_t *
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

static const size_t TIMER_STRLEN          = ui_texts[SCRAN_UI_TEXT_STATUSLINE_TIMER_DUMMY].strlen;
static const size_t SELECTION_SIZE_STRLEN = ui_texts[SCRAN_UI_TEXT_STATUSLINE_SELECTION_SIZE_DUMMY].strlen;

static inline void
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

static inline void
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

static inline bool
redraw_statusline_textline(
    struct scran_ui_context *ui_ctx,
    struct scran_ui_textline_view textline,
    struct scran_ui_statusline_textline *statusline
) {
    bool redrew = false;

    assert(textline.n_items == SCRAN_UI_STATUSLINE_N_ITEMS);
    assert(textline.items[SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE].live_state.text == SCRAN_UI_TEXT_STATUSLINE_SELECTION_SIZE_DUMMY);
    assert(textline.items[SCRAN_UI_STATUSLINE_ITEM_I_TIMER].live_state.text          == SCRAN_UI_TEXT_STATUSLINE_TIMER_DUMMY);

    if (scran_ui_textline_item_is_dirty(textline, SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE)) {
        char16_t selection_size_string[SELECTION_SIZE_STRLEN];
        get_selection_size_string(selection_size_string, statusline->selection_size);
        redraw_textline_item_image_using_atlas(
            ui_ctx,
            textline,
            SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE,
            &(struct ui_string){ .str = selection_size_string, .strlen = SELECTION_SIZE_STRLEN }
        );
        redrew = true;
    }

    if (scran_ui_textline_item_is_dirty(textline, SCRAN_UI_STATUSLINE_ITEM_I_TIMER)) {
        char16_t timer_string[SELECTION_SIZE_STRLEN];
        get_timer_string(timer_string, statusline->timer_seconds);
        redraw_textline_item_image_using_atlas(
            ui_ctx,
            textline,
            SCRAN_UI_STATUSLINE_ITEM_I_TIMER,
            &(struct ui_string){ .str = timer_string, .strlen = TIMER_STRLEN }
        );
        redrew = true;
    }

    textline.meta->dirty_items_mask = 0;

    return redrew;
}

uint8_t
scran_ui_redraw_elements(
    struct scran_ui_context *ui_ctx
) {
    uint8_t redrawn_textline_mask = 0;

    if (redraw_static_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_greeting))) {
        redrawn_textline_mask |= SCRAN_UI_REDREW_GREETING;
    }
    if (redraw_static_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap))) {
        redrawn_textline_mask |= SCRAN_UI_REDREW_KEYMAP;
    }
    if (redraw_statusline_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline), &ui_ctx->ui_statusline)) {
        redrawn_textline_mask |= SCRAN_UI_REDREW_STATUSLINE;
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

    float font_ascent;
    float font_height;
    {
        BLFontMetrics font_metrics;
        bl_font_get_metrics(font, &font_metrics);
        font_ascent = font_metrics.ascent;
        font_height = font_ascent + font_metrics.descent + SCRAN_SELECTION_SHADOW_OFFSET_PX;
    }

    float font_advance_fixed_width;
    {
        static const char16_t single_glyph[] = u"W";
        BLTextMetrics text_metrics = get_bl_text_metrics(font, single_glyph, CHAR16_STRLEN(single_glyph));
        font_advance_fixed_width = text_metrics.advance.x;
    }

    ui_ctx->font_ascent = font_ascent;
    ui_ctx->font_height = font_height;
    ui_ctx->font_advance_fixed_width = font_advance_fixed_width;

    // - Allocate a buffer that fits the largest possible string for all text images
    // - Pre-calculate the pixel-widths of each text
    {
        int height_px_max = scran_ui_font_height_px(ui_ctx);
        int width_px_max  = 0;

        // Some of this could be done at compile-time, but would require some ugly macros...
        for (enum scran_ui_text i = 0; i < SCRAN_UI_N_TEXTS; ++i) {
            const struct ui_string *string = &ui_texts[i];
            int width_px = calculate_bl_text_width_px(font, string->str, string->strlen);
            if (width_px_max < width_px) {
                width_px_max = width_px;
            }
            ui_ctx->cached_text_widths_px[i] = width_px;
        }

        {
            int atlas_advance_px = get_glyph_atlas_advance_px(ui_ctx);
            int atlas_width_px_max = MAX(
                ui_texts[SCRAN_UI_TEXT_ATLAS_DIGITS].strlen * atlas_advance_px,
                ui_texts[SCRAN_UI_TEXT_ATLAS_SEPARATORS].strlen * atlas_advance_px
            );
            width_px_max = MAX(width_px_max, atlas_width_px_max);
        }

        assert(width_px_max != 0);

        // Redraw the glyph atlas first, since it's used by the other UI elements.
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.digits),         width_px_max, height_px_max);
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.separators),     width_px_max, height_px_max);
        redraw_glyph_atlas_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.digits));
        redraw_glyph_atlas_textline(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.separators));

        // TODO: Fix the need to manually list each textline here, in init and in redraw?
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_greeting),          width_px_max, height_px_max);
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap),            width_px_max, height_px_max);
        reinit_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline),        width_px_max, height_px_max);
        scran_ui_redraw_elements(ui_ctx);

    }

    return true;
}

struct default_textline_values {
    enum scran_ui_text  text;
    enum scran_ui_color color;
};
static const struct default_textline_values m_greeting_defaults[] = {
    [SCRAN_UI_GREETING_ITEM_I_GREETING]             = { SCRAN_UI_TEXT_GREETING,                        SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_statusline_defaults[] = {
    [SCRAN_UI_STATUSLINE_ITEM_I_SELECTION_SIZE]     = { SCRAN_UI_TEXT_STATUSLINE_SELECTION_SIZE_DUMMY, SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_STATUSLINE_ITEM_I_TIMER]              = { SCRAN_UI_TEXT_STATUSLINE_TIMER_DUMMY,          SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_keymap_defaults[] = {
    [SCRAN_UI_KEYMAP_ITEM_I_IMAGE]                  = { SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT,            SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_VIDEO]                  = { SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT,            SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME]            = { SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_ON,      SCRAN_UI_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FOCUS]                  = { SCRAN_UI_TEXT_KEYMAP_FOCUS_DEFAULT,            SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_atlas_digits_defaults[] = {
    [0]                                             = { SCRAN_UI_TEXT_ATLAS_DIGITS,                    SCRAN_UI_COLOR_DEFAULT },
};
static const struct default_textline_values m_atlas_separators_defaults[] = {
    [0]                                             = { SCRAN_UI_TEXT_ATLAS_SEPARATORS,                SCRAN_UI_COLOR_DEFAULT },
};
static_assert(ARRAY_LENGTH(m_greeting_defaults)          == SCRAN_UI_GREETING_N_ITEMS,                             "");
static_assert(ARRAY_LENGTH(m_statusline_defaults)        == SCRAN_UI_STATUSLINE_N_ITEMS,                           "");
static_assert(ARRAY_LENGTH(m_keymap_defaults)            == SCRAN_UI_KEYMAP_N_ITEMS,                               "");
static_assert(ARRAY_LENGTH(m_atlas_digits_defaults)      == ARRAY_LENGTH((struct glyph_atlas){}.digits.items),     "");
static_assert(ARRAY_LENGTH(m_atlas_separators_defaults)  == ARRAY_LENGTH((struct glyph_atlas){}.separators.items), "");

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

    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_greeting),           m_greeting_defaults,          ARRAY_LENGTH(m_greeting_defaults));
    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap),             m_keymap_defaults,            ARRAY_LENGTH(m_keymap_defaults));
    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline),         m_statusline_defaults,        ARRAY_LENGTH(m_statusline_defaults));
    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.digits),    m_atlas_digits_defaults,      ARRAY_LENGTH(m_atlas_digits_defaults));
    init_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.separators),m_atlas_separators_defaults,  ARRAY_LENGTH(m_atlas_separators_defaults));

    for (int i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        scran_ui_textline_item_set_disabled(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), i, SCRAN_UI_DISABLE_REASON_NOT_ACTIVE_SURFACE, true);
    }

    reinit_scran_ui(ui_ctx, scale);

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

    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_greeting));
    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap));
    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline));
    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.digits));
    destroy_textline(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->glyph_atlas.separators));
}
