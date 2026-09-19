#ifndef SCRAN_SURFACE_H
#define SCRAN_SURFACE_H


#include <stdint.h>

#include <blend2d/blend2d.h>

#include "state.h"


#define UI_COLOR_SELECTION_DEFAULT UINT32_C(0xE0FFFFFF)
#define UI_COLOR_TEXT_DEFAULT      UINT32_C(0xFFDDDDDD)

#define UI_COLOR_BG_DIM            UINT32_C(0x880E0E0E)
#define UI_COLOR_KEYBOARD_MODIFIER UINT32_C(0xFFFFFFAA)
#define UI_COLOR_FREEZEFRAME       UINT32_C(0xFF6BE7FF)
#define UI_COLOR_VIDEO_CAPTURE     UINT32_C(0xFFFF0000)

#define SCRAN_SELECTION_BORDER_THICKNESS_PX 1

void draw_selection_and_damage_buffer(struct scran_output *output, struct scran_output_selectionSurface_buffer *st_buffer, struct BLBoxI capture_area);
void request_selection_surface_frame_callback(struct scran_output *output);
void init_selection_surface_content(struct scran_output *output);

static inline void
set_force_redraw_selection_surface_buffers(struct scran_output *output) {
    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &output->selection_surface.double_buffer[i];
        st_buffer->force_redraw = true;
    }
}

static inline BLBoxI
get_selection_surface_pre_selection_box(struct scran_output *output) {
    int font_height_px = atlas_font_height_px(&output->selection_surface.atlas);
    assert(font_height_px);

    int margin_px = round(font_height_px * 0.5) + SCRAN_SELECTION_BORDER_THICKNESS_PX;

    // Show all of the UI in the top left corner, including splash/greeting line
    return (BLBoxI){
        .x0 = margin_px + 0,
        .y0 = margin_px + 2 * font_height_px,
        .x1 = margin_px + 0,
        .y1 = margin_px + 2 * font_height_px,
    };
}


#endif
