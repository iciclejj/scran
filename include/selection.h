#ifndef SCRAN_SELECTION_H
#define SCRAN_SELECTION_H

#include "state.h"
#include "wlr-layer-shell-unstable-v1.h"


enum surface_theme {
    SURFACE_THEME_DEFAULT,
    SURFACE_THEME_VIDEO_CAPTURE,
};

#define SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED   ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
#define SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE

#define SCRAN_BTN_NONE 0 // linux/input-event-codes.h: #define KEY_RESERVED 0


void set_selection_surface_theme(struct scran_output *st_output, enum surface_theme action);

void set_selection_initialized(struct scran_output *st_output);
bool set_selection_freeze_size(struct scran_output *st_output);
 void unset_selection_freeze_size(struct scran_output *st_output);

void start_grabbing_focus();
void stop_grabbing_focus();


#endif
