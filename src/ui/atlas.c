#include <stddef.h>

#include <blend2d/blend2d.h>

#include "ui.h"
#include "init.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"

#include "./atlas.h"


const struct ui_string g_scran_ui_atlas_string = UI_STRING(g_ui_strings.unique_glyphs_sorted);


static size_t
get_glyph_index(
    const struct glyph_atlas *atlas,
    char16_t target_char
) {
    // binary search
    int lo = 0;
    int hi = g_scran_ui_atlas_string.strlen;
    int mid = 0;
    char16_t found;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        found = g_scran_ui_atlas_string.str[mid];
        if (found < target_char) {
            lo = mid + 1;
        } else if (found > target_char) {
            hi = mid;
        } else {
            break;
        }
    }
    // TODO: Fallback?
    assert(found == target_char);

    return mid;
}

static struct BLTextMetrics
get_bl_text_metrics(
    const BLFontCore *font,
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

//      Note: bl_font_get_text_metrics' bounding_box.y0/y1 are always 0
//          Use bl_font_get_glyph_bounds for the vertical extents

void
scran_atlas_append_text_metrics(
    struct atlas_text_metrics *left,
    const struct atlas_text_metrics *right
) {
    if (atlas_metrics_bbox_has_ink(right)) {
        int appended_x0 = lround(left->advance.x) + right->bbox.x0;
        int appended_x1 = lround(left->advance.x) + right->bbox.x1;

        if (atlas_metrics_bbox_has_ink(left)) {
            left->bbox.x0 = MIN(left->bbox.x0, appended_x0);
            left->bbox.x1 = MAX(left->bbox.x1, appended_x1);
        } else {
            left->bbox.x0 = appended_x0;
            left->bbox.x1 = appended_x1;
        }
    }

    left->advance.x += right->advance.x;
}

// We need this, since bl_font_get_text_metrics only checks the first and last
// glyph's bbox to determine entire run width.
// This also only computes what we actually need, in our own code, and uses
// cached values from our initialized atlas-glyphs array.
struct atlas_text_metrics
scran_atlas_get_text_metrics_px(
    const struct glyph_atlas *atlas,
    const char16_t *str,
    size_t strlen
) {
    struct atlas_text_metrics run_metrics = {0};

    for (size_t i = 0; i < strlen; ++i) {
        size_t i_glyph = get_glyph_index(atlas, str[i]);
        scran_atlas_append_text_metrics(&run_metrics, &atlas->glyphs[i_glyph].metrics);
    }

    return run_metrics;
}

// Returns total required atlas image width
static int
init_atlas_glyph_metrics(struct glyph_atlas *atlas)
{
    int atlas_cell_x = 0;

    for (size_t i = 0; i < SCRAN_UI_ATLAS_GLYPHS_STRLEN; ++i) {
        struct atlas_glyph *glyph = &atlas->glyphs[i];

        BLTextMetrics text_metrics = get_bl_text_metrics(&atlas->font, &g_scran_ui_atlas_string.str[i], 1);

        glyph->metrics.bbox.x0   = floor(text_metrics.bounding_box.x0);
        glyph->metrics.bbox.x1   = ceil(text_metrics.bounding_box.x1);
        glyph->metrics.advance.x = text_metrics.advance.x;

        atlas->glyphs[i].atlas_x = atlas_cell_x;

        atlas_cell_x += atlas_metrics_bbox_width(&glyph->metrics);
    }

    return atlas_cell_x;
}

static void
redraw_glyph_atlas(struct glyph_atlas *atlas)
{
    const size_t n_glyphs = g_scran_ui_atlas_string.strlen;

    const int font_ascent_px = round(atlas->font_ascent);

    bl_context_begin(&atlas->bl_ctx, &atlas->bl_img, NULL);
    bl_context_clear_all(&atlas->bl_ctx);

    for (size_t i = 0; i < n_glyphs; ++i) {
        const struct atlas_glyph *glyph = &atlas->glyphs[i];
        const char16_t *glyph_char = &g_scran_ui_atlas_string.str[i];

        const BLPointI atlas_pen_origin = {
            // Subtract bbox.x0 so each cell only contains its own glyph.
            //   (The blend2d text-renderer's `origin` arg is the pen/cursor
            //   position, i.e. it can blit behind the origin.)
            .x = glyph->atlas_x - glyph->metrics.bbox.x0,
            .y = font_ascent_px,
        };
        // Draw white glyph
        bl_context_fill_utf16_text_i_rgba32(&atlas->bl_ctx, &atlas_pen_origin, &atlas->font, glyph_char, 1, 0xFFFFFFFF);
    }

    bl_context_end(&atlas->bl_ctx);
}

// Should be called on scale changes to resize fonts etc.
bool
scran_ui_reinit_atlas(
    struct scran_ui_context *ui_ctx,
    double scale
) {
    struct glyph_atlas *atlas = &ui_ctx->glyph_atlas_2;
    BLFontCore *font = &atlas->font;

    if (scale == 0) {
        return false;
    }

    bl_font_reset(font);
    {
        BLFontDataCore font_data;
        bl_font_data_init(&font_data);
        bl_font_data_create_from_data(&font_data, scran_font_ttf, scran_font_ttf_size, NULL, NULL);

        BLFontFaceCore font_face;
        bl_font_face_init(&font_face);
        bl_font_face_create_from_data(&font_face, &font_data, 0);

        const float font_size = scale * 15.f;
        bl_font_create_from_face(font, &font_face, font_size);

        bl_font_data_destroy(&font_data);
        bl_font_face_destroy(&font_face);
    }

    {
        BLFontMetrics font_metrics;
        bl_font_get_metrics(font, &font_metrics);
        atlas->font_ascent = font_metrics.ascent;
        atlas->font_height = font_metrics.ascent + font_metrics.descent + SCRAN_UI_GLYPH_SHADOW_SIZE_PX;
    }

    int required_atlas_width_px  = init_atlas_glyph_metrics(atlas);
    int required_atlas_height_px = scran_ui_atlas_font_height_px(atlas);

    // Don't move this before glyph metrics initialization
    atlas->fallback_glyph_advance = atlas->glyphs[get_glyph_index(atlas, u' ')].metrics.advance;

    bl_image_reset(&atlas->bl_img);
    bl_image_create(
        &atlas->bl_img,
        required_atlas_width_px,
        required_atlas_height_px,
        wl_shm_format_to_blend2d(SURFACE_SHM_FORMAT)
    );
    redraw_glyph_atlas(atlas);
    scran_ui_set_all_items_dirty(ui_ctx);

    return true;
}

void
scran_ui_init_atlas(struct scran_ui_context *ui_ctx, double scale)
{
    struct glyph_atlas *atlas = &ui_ctx->glyph_atlas_2;

#ifndef NDEBUG
    // Assert sorted array of unique glyphs
    // TODO: Generate the atlas string in a cache-friendly bsearch layout.
    {
        char16_t prev = 0;
        for (size_t i = 0; i < g_scran_ui_atlas_string.strlen; ++i) {
            assert(g_scran_ui_atlas_string.str[i] > prev);
            prev = g_scran_ui_atlas_string.str[i];
        }
    }
#endif

    bl_image_init(&atlas->bl_img);
    bl_context_init(&atlas->bl_ctx);
    bl_font_init(&atlas->font);
    scran_ui_reinit_atlas(ui_ctx, scale);
}

void
scran_ui_destroy_atlas(struct glyph_atlas *atlas)
{
    bl_image_destroy(&atlas->bl_img);
    bl_context_destroy(&atlas->bl_ctx);
    bl_font_destroy(&atlas->font);
}

struct atlas_text_metrics
scran_ui_atlas_blit_glyph(
    const struct glyph_atlas *atlas,
    BLContextCore *dst_bl_ctx,
    const BLPointI *dst_pen_origin,
    char16_t glyph_char
) {
    const struct atlas_glyph *glyph = &atlas->glyphs[get_glyph_index(atlas, glyph_char)];

    const BLRectI src_img_area = {
        .x = glyph->atlas_x,
        .y = 0,
        .w = atlas_metrics_bbox_width(&glyph->metrics),
        .h = scran_ui_atlas_font_height_px(atlas),
    };

    const BLPointI dst_origin = {
        .x = dst_pen_origin->x + glyph->metrics.bbox.x0,
        .y = dst_pen_origin->y, // TODO: + glyph->metrics.bbox.y0
    };

    bl_context_blit_image_i(dst_bl_ctx, &dst_origin, &atlas->bl_img, &src_img_area);

    return glyph->metrics;
}
