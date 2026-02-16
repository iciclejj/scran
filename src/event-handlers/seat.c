#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "state.h"

#include "event-handlers.h"

static void
handle_seat_name(
    void *data,
    struct wl_seat *wl_seat,
    const char *name
) {
    // TODO
}

static void
handle_seat_capabilities(
    void *data,
    struct wl_seat *seat,
    uint32_t capability
) {
    // TODO: Read through seat documentation properly
    //         esp. the v4 vs v5 things
    //       Improve capability bitfield/enum documentation (wayland docs/xmls)?
    //          Unclear language wrt. the arg being a bitfield
    struct scran *state = data;

    if (capability & WL_SEAT_CAPABILITY_POINTER) {
        state->seat.wl_pointer = wl_seat_get_pointer(seat);
        // TODO: Consider using non-staging protocols for this? No real reason to use
        //       wp_cursor_shape_manager other than convenience.
        state->seat.pointer_ctx.cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(
            state->globals.cursor_shape_manager,
            state->seat.wl_pointer
        );
        wl_pointer_add_listener(state->seat.wl_pointer, &pointer_listener, state);
    }
    if (capability & WL_SEAT_CAPABILITY_KEYBOARD) {
        state->seat.wl_keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(state->seat.wl_keyboard, &keyboard_listener, state);
    }
    if (capability & WL_SEAT_CAPABILITY_TOUCH) {
        // TODO
    }

    struct scran_seat_datacontrol *const st_datacontrol = &state->seat.datacontrol;

    st_datacontrol->device = ext_data_control_manager_v1_get_data_device(
        state->globals.data_control_manager,
        state->globals.seat
    );

    st_datacontrol->manager = &state->globals.data_control_manager;
}

struct wl_seat_listener seat_listener = {
    .name = handle_seat_name,
    .capabilities = handle_seat_capabilities,
};

void
seat_listener__destroy(struct scran_seat *seat)
{
    ext_data_control_device_v1_destroy(seat->datacontrol.device);
    if (seat->datacontrol.source != NULL) {
        ext_data_control_source_v1_destroy(seat->datacontrol.source);
    }

    wl_keyboard_destroy(seat->wl_keyboard);
    keyboard_listener_destroy(seat);

    wl_pointer_destroy(seat->wl_pointer);
    wp_cursor_shape_device_v1_destroy(seat->pointer_ctx.cursor_shape_device);
}

