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


void selection_surface_set_theme(struct scran_output *st_output, enum surface_theme action);

void selection_set_initialized(struct scran_output *st_output);

void selection_surface_acquire_hide_then(struct scran_output *st_output, struct wp_presentation_feedback_listener *listener, enum scran_selection_surface_disable_reason reason);
void selection_surface_release_hide(struct scran_output *st_output, enum scran_selection_surface_disable_reason reason);

void scran_focus_grab(void);
void scran_focus_grab_for_output(struct scran_output *st_output);
void scran_focus_release(void);


static inline void
selection_freeze_size(struct scran_output *output) {
    if (output->selection_ctx.selection_state == SELECTION_NONE) {
        assert(output->capture.pending_fullscreen_consumers || output->capture.fullscreen_consumers);
    }

    output->selection_ctx.size_is_frozen = true;

    if (output->selection_ctx.selection_state == SELECTION_RESIZING) {
        // Incompatible state; neutralize button.
        g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;
        output->selection_ctx.selection_state = SELECTION_COMPLETE;
    }
}
static inline void
selection_unfreeze_size(struct scran_output *output) {
    output->selection_ctx.size_is_frozen = false;
}

static inline void
selection_do_some_damage(
    struct scran_output *st_output
) {
    wl_surface_damage_buffer(st_output->selection_surface.surface.wl_surface, 0, 0, 1, 1);
    wl_surface_commit(st_output->selection_surface.surface.wl_surface);
}

static inline BLBoxI
get_fullscreen_selection_box(const struct scran_output *st_output) {
    return (BLBoxI){
        .x0 = 0,
        .y0 = 0,
        .x1 = get_transformed_output_width(st_output),
        .y1 = get_transformed_output_height(st_output),
    };
}

static inline bool
selection_is_none(struct scran_output_selectionContext *selection_ctx) {
    return selection_ctx->selection_state == SELECTION_NONE;
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
