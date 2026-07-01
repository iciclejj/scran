#include <assert.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "state.h"
#include "state-util.h"
#include "selection.h"
#include "print.h"
#include "capture.h"
#include "selection-surface.h"
#include "ui.h"
#include "freezeframe.h"
#include "init.h"
#include "dbus.h"


extern struct scran g_state;


void
set_selection_surface_theme(
    struct scran_output *st_output,
    enum surface_theme action
) {
    struct BLRgba32 fill_style;
    static const enum BLFillRule fill_rule = BL_FILL_RULE_EVEN_ODD;

    switch (action) {
    case SURFACE_THEME_PRE_SELECTION:
        // Alpha channel must be respected for invisibility.
        assert(SURFACE_SHM_FORMAT_BL == BL_FORMAT_PRGB32);
        fill_style = SCRAN_SELECTION_BORDER_COLOR_INVISIBLE;
        break;
    case SURFACE_THEME_DEFAULT:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_DEFAULT;
        break;
    case SURFACE_THEME_VIDEO_CAPTURE:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE;
        break;
    default:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_DEFAULT;
        break;
    }

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &st_output->selection_surface.double_buffer[i];

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, fill_style.value);
        bl_context_set_fill_rule(&st_buffer->bl_ctx, fill_rule);
    }

    // We need to force full redraw for the colors to change regardless of dirty
    // rects etc.
    set_force_redraw_selection_surface_buffers(st_output);
}


// TODO: Rename to set_selection_state_complete
void
set_selection_initialized(struct scran_output *st_output)
{
    assert(st_output->selection_ctx.selection_state == SELECTION_INITIALIZING
           || (g_state.options.have_custom_initial_selection && st_output->selection_ctx.selection_state == SELECTION_NONE));

    // TODO: Not sure if we should deinvert in here or let the caller decide

    st_output->selection_ctx.selection_state = SELECTION_COMPLETE;

    // Reset button, since this function could have interrupted an ongoing
    // state-dependent action.
    g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;

    // Make sure this is initialized immediately, to not be dependent on
    // surface::frame being done, for example when using 'scran -eg'.
    //     TODO: Would be better to de-couple this somehow.
    capture_update_area_with_selection(st_output, st_output->selection_ctx.box_px);

    if (g_state.options.capture_and_exit_after_selection_init) {
        DEBUG("STARTING AUTOMATIC IMAGE CAPTURE\n");
        image_capture_start(st_output);
        g_state.exit_requested = true;
    }
}

bool
set_selection_freeze_size(struct scran_output *st_output)
{
    enum selection_state *selection_state = &st_output->selection_ctx.selection_state;
    switch(*selection_state) {
        case SELECTION_REBASING: *selection_state = SELECTION_REBASING_FREEZE_SIZE; break;
        case SELECTION_COMPLETE: *selection_state = SELECTION_COMPLETE_FREEZE_SIZE; break;
        case SELECTION_RESIZING:
            // Incompatible state; neutralize button.
            g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;
            *selection_state = SELECTION_COMPLETE_FREEZE_SIZE; break;
        default:
            eprintf("Can't freeze selection size in current selection state. (SELECTION_STATE=%d)\n", *selection_state);
            return false;
    }

    return true;
}

void
unset_selection_freeze_size(struct scran_output *st_output)
{
    enum selection_state *selection_state = &st_output->selection_ctx.selection_state;

    switch(*selection_state) {
        case SELECTION_COMPLETE_FREEZE_SIZE: *selection_state = SELECTION_COMPLETE; break;
        case SELECTION_REBASING_FREEZE_SIZE: *selection_state = SELECTION_REBASING; break;
        default:
            assert("UNEXPECTED: unset_selection_freeze() called without frozen selection state");
            break;
    }
}

// We need an output-specific function since freezeframe will need to call back
// into it from the output-specific capture_frame::ready handler.
void
start_grabbing_focus_for_output(
    struct scran_output *st_output
) {
    struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

    DEBUG("start_grabbing_focus_for_output()\n");

    // NULL sets an infinite region
    wl_surface_set_input_region(st_surface->wl_surface, NULL);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_surface->layer_surface,
        SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
    );
    wl_surface_commit(st_surface->wl_surface);

    // XXX: See comment in stop_grabbing_focus() below
    wp_cursor_shape_device_v1_set_shape(
        g_state.seat.pointer_ctx.cursor_shape_device,
        g_state.seat.pointer_ctx.last_enter_serial,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );

    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
            scran_ui_keymap_item_set_disabled(ui_ctx, i, SCRAN_UI_DISABLE_REASON_RELEASED_FOCUS, false);
        }
        scran_ui_keymap_item_set_text(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_FOCUS, SCRAN_UI_KEYMAP_TEXT_FOCUS_DEFAULT);
    }

    // TODO: Make arm_selection_surface_frame_callback externally callable, so
    // that we avoid potential double-commit here?
    request_selection_surface_frame_callback(st_output);
}

void
start_grabbing_focus()
{
    DEBUG("Grabbing focus\n");

    FOR_EACH_OUTPUT(i, st_output) {
        if (g_state.options.freezeframe) {
            freezeframe_capture_refresh(st_output, start_grabbing_focus_for_output);
            continue;
        }

        start_grabbing_focus_for_output(st_output);
    }
}

static inline enum scran_ui_keymap_text
get_focus_released_keymap_text(bool have_tray_icon) {
    return have_tray_icon
        ? SCRAN_UI_KEYMAP_TEXT_FOCUS_RELEASED_TRAY
        : SCRAN_UI_KEYMAP_TEXT_FOCUS_RELEASED_HELP;
}

void
update_focus_released_keymap_text(bool have_tray_icon)
{
    const enum scran_ui_keymap_text text = get_focus_released_keymap_text(have_tray_icon);

    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_ui_context     *ui_ctx     = &st_output->selection_surface.ui_ctx;
        struct scran_ui_keymap_item *focus_item = &ui_ctx->ui_keymap.items[SCRAN_UI_KEYMAP_ITEM_I_FOCUS];

        typeof(focus_item->disable_reason_mask) released_focus_bit = 1U << SCRAN_UI_DISABLE_REASON_RELEASED_FOCUS;
        if ((focus_item->disable_reason_mask & released_focus_bit) != 0) {
            scran_ui_keymap_item_set_text(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_FOCUS, text);
            request_selection_surface_frame_callback(st_output);
        }
    }
}

void
stop_grabbing_focus()
{
    DEBUG("Releasing focus\n");

    if (g_state.options.freezeframe) {
        FOR_EACH_OUTPUT(i, st_output) {
            freezeframe_hide_surface(st_output);
        }
    }

    const enum scran_ui_keymap_text focus_released_keymap_text = get_focus_released_keymap_text(scran_dbus_have_tray_icon());

    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

        wl_surface_set_input_region(st_surface->wl_surface, g_state.empty_wl_region);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            st_surface->layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED
        );
        wl_surface_commit(st_surface->wl_surface);

        // XXX: We need to do this to make our cursor update shape without
        // having to move it. Seems like most compositors behave this way...
        // Marked with XXX because theoretically we would want it to be set to
        // the cursor shape of the surface below us, which is not necessarily
        // default, but this is a lot better than nothing. Even in cases where
        // where the other surface uses a non-default cursor, this still gives
        // us nice visual feedback.
        wp_cursor_shape_device_v1_set_shape(
            g_state.seat.pointer_ctx.cursor_shape_device,
            g_state.seat.pointer_ctx.last_enter_serial,
            WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT
        );

        {
            struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
            for (enum scran_ui_keymap_item_index i = 0; i < SCRAN_UI_KEYMAP_N_ITEMS; ++i) {
                scran_ui_keymap_item_set_disabled(ui_ctx, i, SCRAN_UI_DISABLE_REASON_RELEASED_FOCUS, true);
            }
            scran_ui_keymap_item_set_text(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_FOCUS, focus_released_keymap_text);
            request_selection_surface_frame_callback(st_output);
        }
    }
}
