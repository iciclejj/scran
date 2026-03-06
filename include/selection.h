#ifndef SCRAN_SELECTION_H
#define SCRAN_SELECTION_H

#include "state.h"


enum surface_theme {
    SURFACE_THEME_DEFAULT,
    SURFACE_THEME_VIDEO_CAPTURE,
};

void set_selection_surface_theme(struct scran_output *st_output, enum surface_theme action);

void start_grabbing_focus();
void stop_grabbing_focus();


#endif
