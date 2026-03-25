#ifndef SCRAN_SELECTION_H
#define SCRAN_SELECTION_H

#include "state.h"


enum surface_theme {
    SURFACE_THEME_DEFAULT,
    SURFACE_THEME_VIDEO_CAPTURE,
};

// TODO: Figure out whether this should rather be set to "exclusive"
//          (Though both pointer and keyboard focus mechanics will be
//           reworked soon anyways to support handing off/retaking
//           focus)
#define SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED   ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
#define SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE

void set_selection_surface_theme(struct scran_output *st_output, enum surface_theme action);

void signal_selection_initialized(struct scran_output *st_output);

void start_grabbing_focus();
void stop_grabbing_focus();


#endif
