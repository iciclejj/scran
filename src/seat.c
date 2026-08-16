#include "seat.h"
#include "selection-surface.h"
#include "state.h"

void
seat_apply_mod_key_state(
    struct scran_seat *seat,
    struct scran_output_selectionSurface *selection_surface,
    bool state
) {
    if(!selection_surface) {
        return;
    }

    struct scran_output     *st_output = wl_container_of(selection_surface, st_output, selection_surface);
    struct scran_ui_context *ui_ctx    = &st_output->selection_surface.ui_ctx;

    if (state) {
        scran_ui_textline_item_set_text( ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_TEXT_KEYMAP_IMAGE_MOD);
        scran_ui_textline_item_set_color(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_COLOR_KEYMAP_MOD);
        scran_ui_textline_item_set_text( ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_TEXT_KEYMAP_VIDEO_MOD);
        scran_ui_textline_item_set_color(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_COLOR_KEYMAP_MOD);
    } else {
        scran_ui_textline_item_set_text( ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_TEXT_KEYMAP_IMAGE_DEFAULT);
        scran_ui_textline_item_set_color(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_COLOR_DEFAULT);
        scran_ui_textline_item_set_text( ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_TEXT_KEYMAP_VIDEO_DEFAULT);
        scran_ui_textline_item_set_color(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_COLOR_DEFAULT);
    }

    // This is only used during video init, so just set this unconditionally
    // to avoid future possible sticky key bugs...
    // TODO: Probably merge the authority for these things into the ui code,
    // especially if we want to support mouse clicks.
    st_output->capture.frame_ctx.audio_disable_modifier_active = state;

    request_selection_surface_frame_callback(st_output);
}
