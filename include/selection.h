#ifndef SCRAN_SELECTION_H
#define SCRAN_SELECTION_H

#include <blend2d/blend2d.h>

#include "presentation-time.h"

#include "state.h"
#include "state-util.h"
#include "util/blend2d.h"


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
void selection_surface_set_theme(struct scran_output *st_output, enum surface_theme action);

void selection_set_initialized(struct scran_output *st_output);
bool selection_freeze_size(struct scran_output *st_output);
 void selection_unfreeze_size(struct scran_output *st_output);
void selection_surface_hide_then(struct scran_output *st_output, struct wp_presentation_feedback_listener *listener, enum scran_selection_surface_disable_reason reason);
 void selection_surface_release_hide(struct scran_output *st_output, enum scran_selection_surface_disable_reason reason);

void scran_focus_grab(void);
void scran_focus_grab_for_output(struct scran_output *st_output);
void scran_focus_release(void);


static inline BLBoxI
get_fullscreen_selection_box(struct scran_output *st_output) {
    return (BLBoxI){
        .x0 = 0,
        .y0 = 0,
        .x1 = get_transformed_output_width(st_output),
        .y1 = get_transformed_output_height(st_output),
    };
}

static inline bool
selection_is_none(struct scran_output_selectionContext *selection_ctx) {
    return selection_ctx->selection_state == SELECTION_NONE ||
           selection_ctx->selection_state == SELECTION_NONE_FREEZE_SIZE;
}

static inline BLBoxI
selection_get_box_px(const struct scran_output_selectionContext *selection_ctx) {
    assert(!blboxi_is_inverted(selection_ctx->box_px));
    return selection_ctx->box_px;
}

static inline void
selection_set_box_px(
    struct scran_output_selectionContext *selection_ctx,
    BLBoxI box_px
) {
    selection_ctx->box_px = blboxi_get_deinverted(box_px);
}


#endif
