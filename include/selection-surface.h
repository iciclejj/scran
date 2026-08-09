#ifndef SCRAN_SURFACE_H
#define SCRAN_SURFACE_H


#include <blend2d/blend2d.h>

#include "state.h"


#define SCRAN_SELECTION_BACKGROUND_COLOR            ((struct BLRgba32){ 0x880E0E0E })
#define SCRAN_SELECTION_BORDER_COLOR_INVISIBLE      ((struct BLRgba32){ 0x00000000 })
#define SCRAN_SELECTION_BORDER_COLOR_DEFAULT        ((struct BLRgba32){ 0xE0FFFFFF })
#define SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE  ((struct BLRgba32){ 0xFFFF0000 })
#define SCRAN_SELECTION_BORDER_THICKNESS_PX 1


void draw_selection_and_damage_buffer(struct scran_output_selectionSurface *selection_surface, struct scran_output_selectionSurface_buffer *st_buffer, struct BLBoxI capture_area);
void request_selection_surface_frame_callback(struct scran_output *st_output);
void init_selection_surface_content(struct scran_output *st_output);

static inline void
set_force_redraw_selection_surface_buffers(
    struct scran_output *st_output
) {
    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &st_output->selection_surface.double_buffer[i];
        st_buffer->force_redraw = true;
    }
}

static inline BLBoxI
get_selection_surface_pre_selection_box(struct scran_output *st_output) {
    float font_height = ceil(st_output->selection_surface.ui_ctx.font_height);
    assert(font_height);

    // Show all of the UI in the top left corner
    return (BLBoxI){
        .x0 = 0,
        .y0 = font_height,
        .x1 = 0,
        .y1 = font_height,
    };
}


#endif
