#ifndef GLYPH_ATLAS_H
#define GLYPH_ATLAS_H


#include <assert.h>
#include <uchar.h>
#include <math.h>

#include <blend2d/blend2d.h>

#include "scran-ui-text.h"


struct ui_string {
    const char16_t *str;
    size_t strlen;
};
#define CHAR16_STRLEN(s) ( (sizeof(s) / sizeof(char16_t)) - 1)
#define UI_STRING(s) ((struct ui_string){ .str = (s), .strlen = CHAR16_STRLEN(s) })

extern const struct ui_string g_scran_ui_atlas_string;
#define SCRAN_UI_ATLAS_GLYPHS_STRLEN (CHAR16_STRLEN(g_ui_strings.unique_glyphs_sorted))


struct atlas_text_metrics {

    // IF MODIFYING:
    //     Remember to update implicated code and getters, including the getter
    //     for the entire metrics struct.

    // Values are signed.
    //   atlas_pen_origin.x == atlas_x - bbox.x0
    struct atlas_text_metrics_bbox {
        int x0;
        int x1;
    } bbox;

    struct atlas_text_metrics_advance {
        // Keep this as floating point to avoid drift in glyph runs.
        double x;
    } advance;
};

struct atlas_glyph {
    int atlas_x;
    struct atlas_text_metrics metrics;
};

struct atlas_blit_data {
    struct ui_string string;
    uint32_t color;
};

struct atlas {
    BLImageCore bl_img;
    BLContextCore bl_ctx;
    BLFontCore font;

    // TODO: Rename to _px or _scaled?
    float font_ascent;
    float font_height; // ascent + descent
    struct atlas_text_metrics_advance space_glyph_advance;

    // TODO: Make this size more directly connected to the global variable's size
    struct atlas_glyph glyphs[SCRAN_UI_ATLAS_GLYPHS_STRLEN];
};


void atlas_init(struct atlas *atlas, double scale);
bool atlas_reinit(struct atlas *atlas, double scale);
void atlas_destroy(struct atlas *atlas);
struct atlas_text_metrics atlas_blit_string(const struct atlas *atlas, BLContextCore *bl_ctx_destination, const BLPointI *origin, const struct atlas_blit_data *data);
struct atlas_text_metrics atlas_get_text_metrics_px(const struct atlas *atlas, const struct ui_string *string);
void atlas_append_text_metrics(struct atlas_text_metrics *left, const struct atlas_text_metrics *right);

static inline int
atlas_font_height_px(const struct atlas *atlas) {
    // XXX TODO: Replace this function entirely with glyph run metrics
    return ceil(atlas->font_height);
}

static inline bool
atlas_metrics_equal(
    const struct atlas_text_metrics *a,
    const struct atlas_text_metrics *b
) {
    return
        a->advance.x == b->advance.x
        && a->bbox.x0 == b->bbox.x0
        && a->bbox.x1 == b->bbox.x1;
}
static inline int
atlas_metrics_pen_x_px(const struct atlas_text_metrics *metrics) {
    return lround(metrics->advance.x);
}
static inline bool
atlas_metrics_bbox_has_ink(const struct atlas_text_metrics *metrics) {
    return metrics->bbox.x0 < metrics->bbox.x1;
}
static inline int
atlas_metrics_bbox_width(const struct atlas_text_metrics *metrics) {
    assert(metrics->bbox.x0 <= metrics->bbox.x1);
    return metrics->bbox.x1 - metrics->bbox.x0;
}


#endif
