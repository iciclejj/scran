#ifndef GLYPH_ATLAS_H
#define GLYPH_ATLAS_H


#include "ui.h"


struct atlas_text_metrics scran_ui_atlas_blit_glyph(const struct glyph_atlas *atlas, BLContextCore *bl_ctx_destination, const BLPointI *origin, char16_t glyph_char);
struct atlas_text_metrics scran_atlas_get_text_metrics_px(const struct glyph_atlas *atlas, const char16_t *str, size_t strlen);
void scran_ui_init_atlas(struct scran_ui_context *ui_ctx, double scale);
void scran_ui_destroy_atlas(struct glyph_atlas *atlas);
void scran_atlas_append_text_metrics(struct atlas_text_metrics *left, const struct atlas_text_metrics *right);


#endif
