#ifndef SCRAN_SURFACE_H
#define SCRAN_SURFACE_H


#include <stdint.h>

#include <blend2d/blend2d.h>

#include "selection.h"
#include "state.h"


#define SCRAN_SELECTION_BORDER_THICKNESS_PX 1


void draw_selection_and_damage_buffer(struct scran_output *output, struct scran_output_selectionSurface_buffer *st_buffer, struct BLBoxI selection);
void draw_selection_and_commit(struct scran_output *output);
void request_selection_surface_frame_callback(struct scran_output *output);
void init_selection_surface_content(struct scran_output *output);
bool ui_needs_redraw(struct scran_output *output, int64_t now_ns);

static inline void
set_force_redraw_selection_surface_buffers(struct scran_output *output) {
    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &output->selection_surface.double_buffer[i];
        st_buffer->force_redraw = true;
    }
}

static inline void
selection_surface_set_border_color(struct scran_output *output, uint32_t color) {
    output->selection_surface.border_color = color;
    set_force_redraw_selection_surface_buffers(output);
}

static inline bool
selection_surface_draws_fullscreen_capture(struct scran_output *output) {
    return
        output->capture.fullscreen_ui_state == SCRAN_FULLSCREEN_UI_SHOW_PENDING
        || output->capture.fullscreen_ui_state == SCRAN_FULLSCREEN_UI_SHOWN;
}

static inline BLBoxI
selection_surface_box_to_draw(struct scran_output *output) {
    return
        selection_surface_draws_fullscreen_capture(output)
        ? get_fullscreen_selection_box(output)
        : selection_get_box_px(&output->selection_ctx);
}


#endif
