#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "state.h"
#include "state-util.h"
#include "seat.h"
#include "freezeframe.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "selection.h"
#include "selection-surface.h"
#include "ui.h"
#include "util/blend2d.h"


static void
handle_keyboard_keymap(
    void *data,
    struct wl_keyboard *keyboard,
    enum wl_keyboard_keymap_format format,
    int fd,
    uint32_t fd_size
) {
    struct scran *const state = data;

    // Sanity check...
    assert((int)WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 == (int)XKB_KEYMAP_FORMAT_TEXT_V1);

    // No other formats are recognized by wayland atm.
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        DEBUG("Unknown keyboard format - ignoring.\n");
        return;
    }

    char *const shm_keymap_str = mmap(
        NULL, fd_size, PROT_READ, MAP_PRIVATE/*see wl xml*/, fd, 0
    );

    state->seat.keyboard.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    state->seat.keyboard.xkb_keymap = xkb_keymap_new_from_buffer(
        state->seat.keyboard.xkb_context,
        shm_keymap_str,
        fd_size,
        (enum xkb_keymap_format)WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );
    state->seat.keyboard.xkb_state = xkb_state_new(state->seat.keyboard.xkb_keymap);

    munmap(shm_keymap_str, fd_size);
    close(fd);
}


static void
handle_keyboard_enter (
    void *data,
    struct wl_keyboard *wl_keyboard,
    uint32_t serial,
    struct wl_surface *surface_entered,
    struct wl_array *keys
) {
    struct scran *state = data;
    struct scran_seat_keyboard *keyboard_ctx = &state->seat.keyboard;

    seat_update_focused_selection_surface(&keyboard_ctx->focused_selection_surface, surface_entered);
}


static void
handle_keyboard_leave (
    void *data,
    struct wl_keyboard *wl_keyboard,
    uint32_t serial,
    struct wl_surface *surface
) {
    struct scran *state = data;
    struct scran_seat_keyboard *keyboard_ctx = &state->seat.keyboard;

    keyboard_ctx->focused_selection_surface = NULL;
}


static void
handle_keyboard_key(
    void *data,
    struct wl_keyboard * keyboard,
    uint32_t serial,
    uint32_t time,
    uint32_t key,
    enum wl_keyboard_key_state key_state
) {
    struct scran *state = data;
    struct scran_output_selectionSurface *focused_selection_surface = state->seat.keyboard.focused_selection_surface;

    if (focused_selection_surface == NULL) {
        return;
    }

    struct scran_output          *st_output    = wl_container_of(focused_selection_surface, st_output, selection_surface);
    struct scran_ui_context      *ui_ctx       = &focused_selection_surface->ui_ctx;

    // Only xkb key format supported
    assert(state->seat.keyboard.xkb_state != NULL);
    const xkb_keysym_t xkb_key = xkb_state_key_get_one_sym(
        state->seat.keyboard.xkb_state,
        key + 8 // See wl_keyboard::keymap_format
    );

    bool pre_selection = st_output->selection_ctx.selection_state == SELECTION_NONE ||
                         st_output->selection_ctx.selection_state == SELECTION_NONE_FREEZE_SIZE;
    bool fullscreen_capture = pre_selection;

    // TODO: Nested switch for released/pressed
    if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        switch(xkb_key) {
        case XKB_KEY_Return:
            scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_IMAGE, false);
            break;
        case XKB_KEY_space:
            scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, false);
            break;
        case XKB_KEY_Tab:
            scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FOCUS, false);
            break;
        case XKB_KEY_z:
        case XKB_KEY_Z:
            if (state->options.freezeframe) {
                scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, false);
            }
            break;
        default:
            return;
        }

        request_selection_surface_frame_callback(st_output);
        return;
    }

    assert(key_state != WL_KEYBOARD_KEY_STATE_RELEASED);
    switch (xkb_key) {
    case XKB_KEY_Left:
        if (!pre_selection) {
            blboxi_shift(&st_output->selection_ctx.box_px, -1,  0);
            request_selection_surface_frame_callback(st_output);
        }
        break;
    case XKB_KEY_Right:
        if (!pre_selection) {
            blboxi_shift(&st_output->selection_ctx.box_px, +1,  0);
            request_selection_surface_frame_callback(st_output);
        }
        break;
    case XKB_KEY_Up:
        if (!pre_selection) {
            blboxi_shift(&st_output->selection_ctx.box_px,  0, -1);
            request_selection_surface_frame_callback(st_output);
        }
        break;
    case XKB_KEY_Down:
        if (!pre_selection) {
            blboxi_shift(&st_output->selection_ctx.box_px,  0, +1);
            request_selection_surface_frame_callback(st_output);
        }
        break;
    case XKB_KEY_Tab:
        stop_grabbing_focus();
        break;
    case XKB_KEY_z:
    case XKB_KEY_Z:
        if (!state->options.freezeframe) {
            break;
        }

        {
            struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
            scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, true);
            request_selection_surface_frame_callback(st_output);
        }

        bool pretend_all_hidden = true;
        FOR_EACH_OUTPUT(i, st_output) {
            if (st_output->freezeframe.callback != NULL) {
                eprintf("Freezeframe refresh already in progress; ignoring toggle.\n");
                goto z_done;
            }
            if (st_output->freezeframe.showing) {
                pretend_all_hidden = false;
            }
        }

        // XXX TODO: Make this a bit cleaner responsibility-wise.
        //             See also refactor-TODO in unhide_selection_surface().
        //           Also, maybe make this the only (default) way to toggle freezeframe,
        //           and don't let refocus automatically re-freeze?
        if (pretend_all_hidden) {
            FOR_EACH_OUTPUT(i, st_output) {
                freezeframe_capture_refresh(st_output, start_grabbing_focus_for_output);
            }
        } else {
            FOR_EACH_OUTPUT(i, st_output) {
                freezeframe_hide_surface(st_output);
                wl_surface_commit(st_output->selection_surface.surface.wl_surface);
            }
        }

z_done:
        break;
    case XKB_KEY_Escape:
        eprintf("Got escape key...");
        if (st_output->capture.frame_ctx.capturing_video) {
            // TODO: Both stop capture and request exit?
            eprintf(" stopping video capture.\n");
            video_capture_request_stop(st_output);
        } else {
            eprintf(" exiting.\n");
            state->exit_requested = true;
        }
        break;
    case XKB_KEY_Return:
        // TODO: Create two capture sessions so that we can take screenshots while
        // doing video capture? Probably just implement it as part of the video
        // capture pipeline, without two capture sessions.
        if (st_output->capture.frame_ctx.capturing_video) {
            eprintf("Screenshot during video capture not implemented yet, try again later :(\n");
        } else {
            bool exit_after_capture = !xkb_state_mod_name_is_active(state->seat.keyboard.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE);

            if (fullscreen_capture) {
                image_capture_start_fullscreen(st_output, exit_after_capture);
            } else {
                image_capture_start(st_output, exit_after_capture);
                scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_IMAGE, true);
                request_selection_surface_frame_callback(st_output);
            }
        }

        break;
    case XKB_KEY_space:
        bool video_button_got_jammed = false;

        if (st_output->capture.frame_ctx.capturing_video) {
            video_capture_request_stop(st_output);
        } else {
            if (st_output->selection_ctx.selection_state == SELECTION_INITIALIZING) {
                // TODO: Guard against capture_and_exit_after_selection_init?
                set_selection_initialized(st_output);
            }

            bool video_capture_started;

            if (fullscreen_capture) {
                video_capture_started = video_capture_start_fullscreen(st_output);
            } else {
                video_capture_started = video_capture_start(st_output);
            }

            if (!video_capture_started) {
                // TODO: Fire a notification instead?
                eprintf("Failed to start video capture.\n");
            }
        }

        if (!video_button_got_jammed) {
            scran_ui_textline_item_set_pressed(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, true);
            request_selection_surface_frame_callback(st_output);
        }
        break;
    }
}


static void
handle_keyboard_modifiers(
    void *data,
    struct wl_keyboard *keyboard,
    uint32_t serial,
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked,
    uint32_t group
) {
    struct scran *state = data;

    xkb_state_update_mask(
        state->seat.keyboard.xkb_state,
        mods_depressed,
        mods_latched,
        mods_locked,
        // INFO: Wayland doesn't give us more than one group to work with. Not
        // sure if it matters which group we "pretend" to handle, but most
        // clients, as well as xkbcommon's own wayland code, uses the 'locked'
        // group, so let's go with that...
        0,
        0,
        group
    );

    struct scran_output_selectionSurface *focused_selection_surface = state->seat.keyboard.focused_selection_surface;

    if (focused_selection_surface == NULL) {
        return;
    }

    struct scran_output *st_output = wl_container_of(focused_selection_surface, st_output, selection_surface);

    bool mod_key_active = xkb_state_mod_name_is_active(state->seat.keyboard.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE);

    {
        struct scran_ui_context *ui_ctx = &focused_selection_surface->ui_ctx;

        if (mod_key_active) {
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
        st_output->capture.frame_ctx.audio_disable_modifier_active = mod_key_active;

        request_selection_surface_frame_callback(st_output);
    }
}


static void
handle_keyboard_repeat_info(
    void *data,
    struct wl_keyboard *keyboard,
    int32_t rate,
    int32_t delay
) {
    // We are responsible for implementing desired repeat behavior.
    // Ignore for now...
}


struct wl_keyboard_listener keyboard_listener = {
    .keymap = handle_keyboard_keymap,
    .enter = handle_keyboard_enter,
    .leave = handle_keyboard_leave,
    .key = handle_keyboard_key,
    .modifiers = handle_keyboard_modifiers,
    .repeat_info = handle_keyboard_repeat_info,
};

void
keyboard_listener__destroy(struct scran_seat *st_seat)
{
    xkb_context_unref(st_seat->keyboard.xkb_context);
    xkb_keymap_unref(st_seat->keyboard.xkb_keymap);
    xkb_state_unref(st_seat->keyboard.xkb_state);
}

