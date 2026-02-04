#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "state.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"

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

// TODO: Either roundtrip here or ensure that capture etc. pipelines get to
// finish properly (can be either before or after this function exits)
static inline void
_set_state_to_exit_requested(struct scran *state)
{
    state->exit_requested = true;
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
    // TODO: Figure out pointer vs keyboard focus
    struct scran_output *st_output = state->seat.pointer_ctx.focused_output;

    if (key_state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        return;
    }

    // Only xkb key format supported
    assert(state->seat.keyboard.xkb_state != NULL);

    const xkb_keysym_t xkb_key = xkb_state_key_get_one_sym(
        state->seat.keyboard.xkb_state,
        key + 8 // See wl_keyboard::keymap_format
    );

    assert(key_state != WL_KEYBOARD_KEY_STATE_RELEASED);
    switch (xkb_key) {
    // TODO: Probably reorganize all of this later
    case XKB_KEY_Escape:
        eprintf("Got escape key...");
        if (st_output->capture.frame_ctx.capturing_video) {
            // TODO: Probably both stop capture and request exit
            //           Have dedicated start/stop capture key that doesn't exit
            eprintf(" stopping video capture.\n");
            st_output->capture.frame_ctx.capturing_video = false;
        } else {
            eprintf(" exiting.\n");
            _set_state_to_exit_requested(state);
        }
        break;
    case XKB_KEY_Return:
        // TODO: Create two capture sessions so that we can take screenshots while
        // doing video capture? Probably just implement it as part of the video
        // capture pipeline, without two capture sessions.
        if (st_output->capture.frame_ctx.capturing_video) {
            eprintf("Screenshot during video capture not implemented yet, try again later :(\n");
        } else {
            start_image_capture(st_output);

            if (!xkb_state_mod_name_is_active(state->seat.keyboard.xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_EFFECTIVE)) {
                state->exit_requested = true;
            }
        }

        break;
    case XKB_KEY_space:
        if (st_output->capture.frame_ctx.capturing_video) {
            st_output->capture.frame_ctx.capturing_video = false;
            // TODO: Need to ensure capture is fully properly fully finished
            //       before we allow new dispatch_capture_event_loop()
        } else {
            start_video_capture(st_output);
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
