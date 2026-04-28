#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "state.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "selection.h"
#include "surface__selection.h"
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
    struct wl_surface *surface,
    struct wl_array *keys
) {
    // TODO
}


static void
handle_keyboard_leave (
    void *data,
    struct wl_keyboard *wl_keyboard,
    uint32_t serial,
    struct wl_surface *surface
) {
    // TODO
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
    struct scran_output_selectionSurface *focused_selection_surface = state->seat.pointer_ctx.focused_fulloutput_selection_surface;

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

    // TODO: Nested switch for released/pressed
    if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        switch(xkb_key) {
        case XKB_KEY_Return:
            scran_ui_keymap_item_set_pressed(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, false);
            request_selection_surface_update(st_output);
            break;
        case XKB_KEY_space:
            DEBUG("GOT SPACE RELEASE\n");
            scran_ui_keymap_item_set_pressed(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, false);
            request_selection_surface_update(st_output);
            break;
        case XKB_KEY_Tab:
            scran_ui_keymap_item_set_pressed(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_FOCUS, false);
            request_selection_surface_update(st_output);
            break;
        }
        return;
    }

    assert(key_state != WL_KEYBOARD_KEY_STATE_RELEASED);
    switch (xkb_key) {
    // TODO: Probably reorganize all of this later
    case XKB_KEY_Left:
        shift_blboxi(&st_output->selection_ctx.box_px, -1,  0);
        request_selection_surface_update(st_output);
        break;
    case XKB_KEY_Right:
        shift_blboxi(&st_output->selection_ctx.box_px, +1,  0);
        request_selection_surface_update(st_output);
        break;
    case XKB_KEY_Up:
        shift_blboxi(&st_output->selection_ctx.box_px,  0, -1);
        request_selection_surface_update(st_output);
        break;
    case XKB_KEY_Down:
        shift_blboxi(&st_output->selection_ctx.box_px,  0, +1);
        request_selection_surface_update(st_output);
        break;
    case XKB_KEY_Tab:
        stop_grabbing_focus();
        break;
    case XKB_KEY_Escape:
        eprintf("Got escape key...");
        if (st_output->capture.frame_ctx.capturing_video) {
            // TODO: Both stop capture and request exit?
            eprintf(" stopping video capture.\n");
            request_end_video_capture(st_output);
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
            request_image_capture(st_output);

            if (!xkb_state_mod_name_is_active(state->seat.keyboard.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE)) {
                state->exit_requested = true;
            }

            scran_ui_keymap_item_set_pressed(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, true);
            request_selection_surface_update(st_output);
        }

        break;
    case XKB_KEY_space:
        bool video_button_got_jammed = false;

        if (st_output->capture.frame_ctx.capturing_video) {
            request_end_video_capture(st_output);
        } else {
            if (st_output->selection_ctx.selection_state == SELECTION_INITIALIZING) {
                set_selection_initialized(st_output);
            }

            if (!request_video_capture(st_output)) {
                video_button_got_jammed = true; // :(
                eprintf("Failed to start video capture.\n");
            }
        }

        if (!video_button_got_jammed) {
            scran_ui_keymap_item_set_pressed(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_VIDEO, true);
            request_selection_surface_update(st_output);
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

    struct scran_output_selectionSurface *focused_selection_surface = state->seat.pointer_ctx.focused_fulloutput_selection_surface;

    if (focused_selection_surface == NULL) {
        return;
    }

    bool mod_key_active = xkb_state_mod_name_is_active(state->seat.keyboard.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE);

    {
        struct scran_ui_context *ui_ctx = &focused_selection_surface->ui_ctx;

        if (mod_key_active) {
            scran_ui_keymap_item_set_text( ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_KEYMAP_TEXT_IMAGE_MOD);
            scran_ui_keymap_item_set_color(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_KEYMAP_COLOR_MOD);
        } else {
            scran_ui_keymap_item_set_text( ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_KEYMAP_TEXT_IMAGE_DEFAULT);
            scran_ui_keymap_item_set_color(ui_ctx, SCRAN_UI_KEYMAP_ITEM_I_IMAGE, SCRAN_UI_KEYMAP_COLOR_DEFAULT);
        }

        struct scran_output *st_output = wl_container_of(focused_selection_surface, st_output, selection_surface);
        request_selection_surface_update(st_output);
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
keyboard_listener_destroy(struct scran_seat *st_seat)
{
    xkb_context_unref(st_seat->keyboard.xkb_context);
    xkb_keymap_unref(st_seat->keyboard.xkb_keymap);
    xkb_state_unref(st_seat->keyboard.xkb_state);
}

