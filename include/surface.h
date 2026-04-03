#ifndef SCRAN_SURFACE_H
#define SCRAN_SURFACE_H


#include <blend2d/blend2d.h>

#include "state.h"


void draw_frame_and_damage_buffer( struct scran_output_surface *st_surface, struct scran_output_surface_buffer *st_buffer, struct BLBoxI capture_area, struct BLBoxI capture_area_bounds);


#endif
