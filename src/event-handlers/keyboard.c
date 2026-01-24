#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

#include <wayland-client.h>

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
    struct client_state *state = data;

    // No other formats are recognized by wayland atm.
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        DEBUG("Unknown keyboard format - ignoring.\n");
        return;
    }

    char *shm_keymap_str = mmap(
        NULL, fd_size, PROT_READ, MAP_PRIVATE/*see wl xml*/, fd, 0
    );

    // TODO: Maybe initialize this once elsewhere?
    //           But keep in mind keymap event can come multiple times.
    // TODO: Destroy.
    state->seat.keyboard.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    state->seat.keyboard.xkb_keymap = xkb_keymap_new_from_string(
        state->seat.keyboard.xkb_context,
        shm_keymap_str,
        // NOTE: Should match the handler's `format` arg
        //       TODO: Is `format` supposed to be safe to use directly here?
        XKB_KEYMAP_FORMAT_TEXT_V1,
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

static inline void
_set_state_to_exit_requested(struct client_state *state)
{
    state->exit_requested = true;
    for (int i = 0; i < state->n_outputs; ++i) {
        // TODO: Revisit this to check for a more elegant solution
        //          (This isn't that bad, though.)
        state->outputs[i].selection.selection_state = SELECTION_EXIT_REQUESTED;
    }
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
    struct client_state *state = data;
    // TODO: Figure out pointer vs keyboard focus
    struct client_state_output *st_output = state->seat.pointer.focused_output;

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
        if (st_output->capture.frame_ctx.capturing) {
            // TODO: Probably both stop capture and request exit
            //           Have dedicated start/stop capture key that doesn't exit
            eprintf(" stopping capture.\n");
            st_output->capture.frame_ctx.capturing = false;
        } else {
            eprintf(" exiting.\n");
            _set_state_to_exit_requested(state);
        }
        break;
    case XKB_KEY_Return:
        // TODO: Create two capture sessions so that we can take screenshots while
        // doing video capture? Probably just implement it as part of the video
        // capture pipeline, without two capture sessions.
        if (st_output->capture.frame_ctx.capturing) {
            eprintf("Screenshot during video capture not implemented yet, try again later :(\n");
        } else {
            start_image_capture(st_output);
        }

        break;
    case XKB_KEY_space:
        if (st_output->capture.frame_ctx.capturing) {
            st_output->capture.frame_ctx.capturing = false;
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
    struct client_state *state = data;

    // TODO(self): Understand this
    xkb_state_update_mask(
        state->seat.keyboard.xkb_state,
        mods_depressed,
        mods_latched,
        mods_locked,
        0,
        0,
        group // TODO: Is this correct ?
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
keyboard_listener_destroy(struct client_state_seat *st_seat)
{
    xkb_context_unref(st_seat->keyboard.xkb_context);
    xkb_keymap_unref(st_seat->keyboard.xkb_keymap);
    xkb_state_unref(st_seat->keyboard.xkb_state);
}
