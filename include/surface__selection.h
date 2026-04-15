#ifndef SCRAN_SURFACE_H
#define SCRAN_SURFACE_H


#include <blend2d/blend2d.h>

#include "state.h"


#define SCRAN_SELECTION_BACKGROUND_COLOR            ((struct BLRgba32){ 0x880E0E0E })
#define SCRAN_SELECTION_BORDER_COLOR_DEFAULT        ((struct BLRgba32){ 0xE0FFFFFF })
#define SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE  ((struct BLRgba32){ 0xFFFF0000 })
#define SCRAN_SELECTION_BORDER_THICKNESS_PX 1


void draw_selection_and_damage_buffer(struct scran_output_selectionSurface *selection_surface, struct scran_output_selectionSurface_buffer *st_buffer, struct BLBoxI capture_area, struct BLBoxI capture_area_bounds);
void request_selection_surface_update(struct scran_output *st_output);
void request_selection_surface_frame_callback( struct scran_output *st_output);
void draw_selection_surface_initial_state(struct scran_output *st_output, struct scran_output_selectionSurface_buffer *st_buffer);


#endif
