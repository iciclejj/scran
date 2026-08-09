#ifndef SCRAN_SELECTION_H
#define SCRAN_SELECTION_H

#include <blend2d/blend2d.h>

#include "presentation-time.h"

#include "state.h"


#define SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED   ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
#define SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE

#define SCRAN_BTN_NONE 0 // linux/input-event-codes.h: #define KEY_RESERVED 0

#define SCRAN_INITIAL_SELECTION_NONE ((BLBoxI){ -1, -1, -1, -1 })


enum surface_theme {
    // HACK: Using this to make selection invisible
    //       TODO: Rework the surface redraw functions for more granular
    //       control over what to draw instead.
    SURFACE_THEME_PRE_SELECTION,

    SURFACE_THEME_DEFAULT,
    SURFACE_THEME_VIDEO_CAPTURE,
};
void set_selection_surface_theme(struct scran_output *st_output, enum surface_theme action);

void set_selection_initialized(struct scran_output *st_output);
bool set_selection_freeze_size(struct scran_output *st_output);
 void unset_selection_freeze_size(struct scran_output *st_output);
void hide_selection_surface_then(struct scran_output *st_output, struct wp_presentation_feedback_listener *listener, enum scran_selection_surface_disable_reason reason);
 void release_selection_surface_hide(struct scran_output *st_output, enum scran_selection_surface_disable_reason reason);

void start_grabbing_focus(void);
void start_grabbing_focus_for_output(struct scran_output *st_output);
void stop_grabbing_focus(void);
void update_focus_released_keymap_text(bool have_tray_icon);


#endif
