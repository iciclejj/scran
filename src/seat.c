#include "seat.h"
#include "selection-surface.h"
#include "state.h"
#include "ui.h"


void
seat_apply_mod_key_state(
    struct scran_seat *seat,
    struct scran_output_selectionSurface *selection_surface,
    bool state
) {
    if(!selection_surface) {
        return;
    }

    struct scran_output           *st_output       = wl_container_of(selection_surface, st_output, selection_surface);
    struct scran_ui_context       *ui_ctx          = &st_output->selection_surface.ui_ctx;
    struct scran_ui_textline_view  keymap_textline = SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap);

    if (state) {
        scran_ui_textline_item_set_text( ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_TEXT_KEYMAP_IMAGE_MOD);
        scran_ui_textline_item_set_color(ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_COLOR_KEYMAP_MOD);
        scran_ui_textline_item_set_text( ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_TEXT_KEYMAP_VIDEO_MOD);
        scran_ui_textline_item_set_color(ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_COLOR_KEYMAP_MOD);
    } else {
        scran_ui_textline_item_set_text( ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT);
        scran_ui_textline_item_set_color(ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_COLOR_DEFAULT);
        scran_ui_textline_item_set_text( ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT);
        scran_ui_textline_item_set_color(ui_ctx, keymap_textline, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_COLOR_DEFAULT);
    }

    // This is only used during video init, so just set this unconditionally
    // to avoid future possible sticky key bugs...
    // TODO: Probably merge the authority for these things into the ui code,
    // especially if we want to support mouse clicks.
    st_output->capture.frame_ctx.audio_disable_modifier_active = state;

    request_selection_surface_frame_callback(st_output);
}

// TODO: Probably store pressed state in scran_seat.

static inline uint32_t
get_keymap_pressed_state(
    struct scran_seat *seat,
    struct scran_output_selectionSurface *selection_surface
) {
    if(!selection_surface) {
        return 0;
    }
    return scran_ui_textline_item_get_pressed_mask(
        SCRAN_UI_TEXTLINE_VIEW(selection_surface->ui_ctx.ui_keymap)
    );
}

static inline void
set_keymap_pressed_state(
    struct scran_output_selectionSurface *selection_surface,
    uint32_t pressed_mask
) {
    if(!selection_surface) {
        return;
    }

    struct scran_output           *st_output       = wl_container_of(selection_surface, st_output, selection_surface);
    struct scran_ui_context       *ui_ctx          = &st_output->selection_surface.ui_ctx;
    struct scran_ui_textline_view  keymap_textline = SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap);

    scran_ui_textline_item_set_pressed_mask(ui_ctx, keymap_textline, pressed_mask);

    request_selection_surface_frame_callback(st_output);
}

static inline void
set_keymap_disabled_state(
    struct scran_output_selectionSurface *selection_surface,
    bool disabled
) {
    if (!selection_surface) {
        return;
    }

    struct scran_output           *st_output       = wl_container_of(selection_surface, st_output, selection_surface);
    struct scran_ui_context       *ui_ctx          = &st_output->selection_surface.ui_ctx;
    struct scran_ui_textline_view  keymap_textline = SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap);

    for (int i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
        scran_ui_textline_item_set_disabled(ui_ctx, keymap_textline, i, SCRAN_UI_DISABLE_REASON_NOT_ACTIVE_SURFACE, disabled);
    }
}

void
seat_update_active_selection_surface(struct scran_seat *seat)
{
    // When multiple same-layer layer surface request ECXLUSIVE keyboard
    // interactivity, it is implementation-defined which surface gets keyboard
    // focus, so prioritize the pointer's focused surface, if available.
    //
    // COSMIC, as of recently started only giving keyboard focus to the "main"
    // display's selection surface.
    //
    // We control all our surfaces, so it doesn't matter if our "real" keyboard
    // focus is on a different surface/output.

    struct scran_output_selectionSurface *new_surface =
        seat->pointer_ctx.focused_selection_surface
        ?: seat->keyboard.focused_selection_surface;

    if (new_surface != seat->active_selection_surface) {
        const uint32_t old_keymap_state = get_keymap_pressed_state(seat, seat->active_selection_surface);

        seat_apply_mod_key_state(seat, seat->active_selection_surface, false);
        set_keymap_pressed_state(seat->active_selection_surface, 0);
        set_keymap_disabled_state(seat->active_selection_surface, true);

        seat_apply_mod_key_state(seat, new_surface, seat->mod_key_active);
        set_keymap_pressed_state(new_surface, old_keymap_state);
        set_keymap_disabled_state(new_surface, false);

        seat->active_selection_surface = new_surface;
    }
}

