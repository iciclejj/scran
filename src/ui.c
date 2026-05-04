#include <blend2d/core/api.h>
#include <uchar.h>
#include <assert.h>
#include <stddef.h>

#include <blend2d/blend2d.h>

#include "init.h"
#include "ui.h"
#include "surface__selection.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"


#define SCRAN_SELECTION_SHADOW_OFFSET_PX 1
#define SCRAN_SELECTION_SHADOW_COLOR     ((struct BLRgba32){ 0xDD0E0E0E })

#define CHAR16_STRLEN(s) ( (sizeof(s) / sizeof(char16_t)) - 1)

extern const char   _binary_assets_font_ttf_start[];
extern const char   _binary_assets_font_ttf_end[];
static inline size_t _get_font_size() {
    return _binary_assets_font_ttf_end - _binary_assets_font_ttf_start;
}
static inline const void * _get_font_data() {
    return _binary_assets_font_ttf_start;
}

static const BLRgba32 keymap_colors[] = {
    [SCRAN_UI_KEYMAP_COLOR_DEFAULT]       = { 0xE0DDDDDD },
    [SCRAN_UI_KEYMAP_COLOR_MOD]           = { 0xE0FFFFAA },
    [SCRAN_UI_KEYMAP_COLOR_ALT]           = { 0xE0888888 },
    [SCRAN_UI_KEYMAP_COLOR_VIDEO_CAPTURE] = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE,
};
static_assert( sizeof(keymap_colors) / sizeof(keymap_colors[0]) == SCRAN_UI_KEYMAP_N_COLORS,
               "keymap_colors[] length must exactly cover all color enum values." );

struct _sized_u16_string {
    const char16_t *str;
    size_t strlen;
};
#define INIT_SIZED_U16_STRING(s) { .str = (s), .strlen = CHAR16_STRLEN(s) }
static const struct _sized_u16_string keymap_image_texts[] = {
    // XXX: Leading space so it doesn't hug the edge when drawn at x=0,y=0.
    //      The top margin is fine already.
    //      Somewhat of a HACK, but shouldn't cause any issues unless we change
    //      the font or origin point.
    [SCRAN_UI_KEYMAP_TEXT_EXTRA_PRE_INIT_DEFAULT] = INIT_SIZED_U16_STRING(u" Click and drag to make selection"),

    [SCRAN_UI_KEYMAP_TEXT_IMAGE_DEFAULT]          = INIT_SIZED_U16_STRING(u"[↵] Image & Exit"),
    [SCRAN_UI_KEYMAP_TEXT_IMAGE_MOD]              = INIT_SIZED_U16_STRING(u"[↵] Image       "),

    [SCRAN_UI_KEYMAP_TEXT_VIDEO_DEFAULT]          = INIT_SIZED_U16_STRING(u"[␣] Video \uf028"),
    [SCRAN_UI_KEYMAP_TEXT_VIDEO_MOD]              = INIT_SIZED_U16_STRING(u"[␣] Video \uf026"),

    [SCRAN_UI_KEYMAP_TEXT_FOCUS_DEFAULT]          = INIT_SIZED_U16_STRING(u"[⇥] Release focus"),
    [SCRAN_UI_KEYMAP_TEXT_FOCUS_RELEASED]         = INIT_SIZED_U16_STRING(u"[⇥] Focus released. 'scran -h' for help."),


    [SCRAN_UI_KEYMAP_TEXT_EMPTY]                  = INIT_SIZED_U16_STRING(u""),
};
static_assert( (sizeof(keymap_image_texts) / sizeof(keymap_image_texts[0])) == SCRAN_UI_KEYMAP_N_TEXTS,
               "keymap_image_texts[] length must exactly cover all text enum values.");

// TODO: Benchmark this once we're done with refactor and see if we want to
// cache any of it.
static inline struct BLTextMetrics
_get_bl_text_metrics(
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


static inline void
_redraw_keymap_image(
    struct scran_ui_context     *ui_ctx,
    struct scran_ui_keymap_item *keymap_item,
    bool            pressed,
    int             height_px
) {
    const struct scran_ui_keymap_item_lockable_state lockable_state =
        keymap_item->locked ? keymap_item->locked_state : keymap_item->live_state;

    const char16_t *text = keymap_image_texts[lockable_state.text].str;
    size_t   text_strlen = keymap_image_texts[lockable_state.text].strlen;

    BLTextMetrics text_metrics = _get_bl_text_metrics(&ui_ctx->font, text, text_strlen);
    const double  width        = text_metrics.advance.x;
    const int     width_px     = ceil(width) + (!!width * SCRAN_SELECTION_SHADOW_OFFSET_PX);

    if (width_px != 0) {
        // TODO: Pre-allocate the largest buffer we might need here so we don't need
        //       to keep resetting/recreating.
        //           Or at least check if we actually resized.
        bl_image_reset(&keymap_item->bl_img);
        bl_image_create(&keymap_item->bl_img, width_px, height_px, wl_shm_format_to_blend2d(SURFACE_SHM_FORMAT));

        bl_context_begin(&ui_ctx->bl_ctx, &keymap_item->bl_img, NULL);

        BLPointI origin = {
            .x = 0,
            .y = ui_ctx->ascent_px,
        };
        BLPointI origin_shadow = {
            .x = 0               + SCRAN_SELECTION_SHADOW_OFFSET_PX,
            .y = ui_ctx->ascent_px + SCRAN_SELECTION_SHADOW_OFFSET_PX,
        };

        // TODO: Is bl_context_clear_all() needed after bl_image_reset()?

        BLRgba32 color = keymap_colors[lockable_state.color];
        bl_context_clear_all(&ui_ctx->bl_ctx);

        if (keymap_item->disable_reason_mask != 0U) {
            BLRgba32 _color = color;
            scale_blrgba32_colors(&_color, 0.64f); // dim the color
            bl_context_fill_utf16_text_i_rgba32(
                &ui_ctx->bl_ctx, &origin_shadow, &ui_ctx->font, text, text_strlen, _color.value
            );
        } else if (pressed) {
            bl_context_fill_utf16_text_i_rgba32(
                &ui_ctx->bl_ctx, &origin_shadow, &ui_ctx->font, text, text_strlen, color.value
            );
        } else {
            bl_context_fill_utf16_text_i_rgba32(
                &ui_ctx->bl_ctx, &origin_shadow, &ui_ctx->font, text, text_strlen, SCRAN_SELECTION_SHADOW_COLOR.value
            );
            bl_context_fill_utf16_text_i_rgba32(
                &ui_ctx->bl_ctx, &origin       , &ui_ctx->font, text, text_strlen, color.value
            );
        }

        bl_context_end(&ui_ctx->bl_ctx);
    }

    keymap_item->width_px = width_px;
}

void
redraw_keymap(
    struct scran_ui_context *ui_ctx
) {
    // TODO: dirty marker per item
    for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[i];

        bool pressed  = ui_ctx->ui_keymap.pressed_items_mask  & (1 << i);

        _redraw_keymap_image(
            ui_ctx,
            keymap_item,
            pressed,
            ui_ctx->ui_keymap.height_px
        );
    }
}


// Should be called on scale changes to resize fonts etc.
bool
reinit_scran_ui(
    struct scran_ui_context *ui_ctx,
    double scale
) {
    BLFontCore *font = &ui_ctx->font;

    bl_font_reset(font);
    {
        BLFontDataCore font_data;
        bl_font_data_init(&font_data);
        bl_font_data_create_from_data(&font_data, _get_font_data(), _get_font_size(), NULL, NULL);

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
        BLTextMetrics text_metrics = _get_bl_text_metrics(font, single_glyph, CHAR16_STRLEN(single_glyph));
        fixed_width_font_glyph_width_px = ceil(text_metrics.advance.x);
    }

    ui_ctx->ascent_px = ascent_px;
    ui_ctx->font_height_px = font_height_px;
    ui_ctx->fixed_width_font_glyph_width_px = fixed_width_font_glyph_width_px;
    ui_ctx->ui_keymap.height_px = font_height_px;

    redraw_keymap(ui_ctx);

    return true;
}

static const struct {
    enum scran_ui_keymap_text  text;
    enum scran_ui_keymap_color color;
} _pre_selection_items[] = {
    [SCRAN_UI_KEYMAP_ITEM_I_IMAGE] = { SCRAN_UI_KEYMAP_TEXT_EMPTY                 , SCRAN_UI_KEYMAP_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_VIDEO] = { SCRAN_UI_KEYMAP_TEXT_EMPTY                 , SCRAN_UI_KEYMAP_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FOCUS] = { SCRAN_UI_KEYMAP_TEXT_EMPTY                 , SCRAN_UI_KEYMAP_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_EXTRA] = { SCRAN_UI_KEYMAP_TEXT_EXTRA_PRE_INIT_DEFAULT, SCRAN_UI_KEYMAP_COLOR_DEFAULT },
};

static const struct {
    enum scran_ui_keymap_text  text;
    enum scran_ui_keymap_color color;
} _post_selection_items[] = {
    [SCRAN_UI_KEYMAP_ITEM_I_IMAGE] = { SCRAN_UI_KEYMAP_TEXT_IMAGE_DEFAULT, SCRAN_UI_KEYMAP_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_VIDEO] = { SCRAN_UI_KEYMAP_TEXT_VIDEO_DEFAULT, SCRAN_UI_KEYMAP_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_FOCUS] = { SCRAN_UI_KEYMAP_TEXT_FOCUS_DEFAULT, SCRAN_UI_KEYMAP_COLOR_DEFAULT },
    [SCRAN_UI_KEYMAP_ITEM_I_EXTRA] = { SCRAN_UI_KEYMAP_TEXT_EMPTY        , SCRAN_UI_KEYMAP_COLOR_DEFAULT },
};

bool
init_scran_ui_pre_selection(
    struct scran_ui_context *ui_ctx,
    double scale
) {
    bl_font_init(&ui_ctx->font);
    bl_context_init(&ui_ctx->bl_ctx);

    for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[i];

        bl_image_init(&keymap_item->bl_img);

        assert(keymap_item->locked == false);
        keymap_item->live_state.text  = _pre_selection_items[i].text;
        keymap_item->live_state.color = _pre_selection_items[i].color;
    }

    reinit_scran_ui(ui_ctx, scale);

    return true;
}

bool
scran_ui_set_selection_stage_defaults(
    struct scran_ui_context *ui_ctx
) {
    for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        struct scran_ui_keymap_item *keymap_item = &ui_ctx->ui_keymap.items[i];

        keymap_item->locked = false;
        keymap_item->live_state.text  = _post_selection_items[i].text;
        keymap_item->live_state.color = _post_selection_items[i].color;
    }

    ui_ctx->dirty = true;

    return true;
}

void
destroy_scran_ui(
    struct scran_ui_context *ui_ctx
) {
    bl_font_destroy(&ui_ctx->font);
    bl_context_destroy(&ui_ctx->bl_ctx);

    for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        bl_image_destroy(&ui_ctx->ui_keymap.items[i].bl_img);
    }
}

