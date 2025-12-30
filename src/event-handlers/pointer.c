#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "state.h"

#include "wayland-event-handlers.h"

void
handle_pointer_enter(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_entered,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct client_state *state = data;
    struct client_state_seat_pointer *st_pointer = &state->seat.pointer;

    // "When a seat's focus enters a surface, the pointer image is undefined..."
    wp_cursor_shape_device_v1_set_shape(
        st_pointer->cursor_shape_device,
        serial,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );

    if (surface_entered == state->surface.surface) {
        state->surface.is_focused = true;
    }
}

void
handle_pointer_leave(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_left
) {
    struct client_state *state = data;

    // TODO: reset_selection((struct client_state *)data);

    if (surface_left == state->surface.surface) {
        state->surface.is_focused = false;
    }
}

void
handle_pointer_motion(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct client_state *state = data;

    state->seat.pointer.x = x;
    state->seat.pointer.y = y;

    if (state->selection.selection_state == SELECTION_IN_PROGRESS) {
        // XXX: Document this and helper function...
        int32_t _x = (int)wl_fixed_to_double(state->seat.pointer.x);
        int32_t _y = (int)wl_fixed_to_double(state->seat.pointer.y);

        // TODO: Make this explicitly either output pixel coordinates or
        //       surface-local coordinates
        state->selection.bl.box.x1 = _x;
        state->selection.bl.box.y1 = _y;
    }

    // TODO: Dynamically resize visual selection
}

void
handle_pointer_button(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    enum wl_pointer_button_state button_state
) {
    struct client_state *state = data;
    struct client_state_seat_pointer *st_pointer = &state->seat.pointer;
    struct client_state_selection_blend2d *bl = &state->selection.bl;

    // TODO: Implement dragging

    // XXX: Not needed when we only have one surface.
    if (!state->surface.is_focused) {
        return;
    }
    if (state->selection.selection_state == SELECTION_COMPLETE) {
        return;
    }

    // XXX: This should probably be a helper function.
    int32_t x = (int)wl_fixed_to_double(state->seat.pointer.x);
    int32_t y = (int)wl_fixed_to_double(state->seat.pointer.y);

    if (button == BTN_LEFT && button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (state->selection.selection_state == SELECTION_NONE) {
            state->selection.bl.box.x0 = x;
            state->selection.bl.box.y0 = y;

            // XXX TEST: Might keep, though..
            state->selection.bl.box.x1 = x;
            state->selection.bl.box.y1 = y;

            state->selection.selection_state = SELECTION_IN_PROGRESS;
        } else {
            state->selection.bl.box.x1 = x;
            state->selection.bl.box.y1 = y;

            // state->selection.bl.box = (BLBoxI){ 0 };
            state->selection.selection_state = SELECTION_COMPLETE;
        }
    }
}

struct wl_pointer_listener pointer_listener = {
    .enter = handle_pointer_enter,
    .leave = handle_pointer_leave,
    .button = handle_pointer_button,
    .motion = handle_pointer_motion,
    .frame = noop, // TODO?
};

