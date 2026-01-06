#include <stdio.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "wayland-event-handlers.h"

static void
registry_handle_global(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version // of interface
    )
{
    struct client_state *state = data;
    struct client_state_globals *globals = &state->globals;

    #define _INTERFACE_IS(desired) (strcmp(interface, desired.name) == 0)

    // TODO: Determine desired minimum versions.
    if (_INTERFACE_IS(wl_compositor_interface)) {
        globals->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
    } else if (_INTERFACE_IS(wl_seat_interface)) {
        if (globals->seat != NULL) {
            // TODO: wl_list of seats
            fprintf(stderr, "Ignoring additional wl_seat global.\n");
        } else {
            fprintf(stderr, "Adding seat... ");
            globals->seat    = wl_registry_bind(registry, name, &wl_seat_interface, version);
            // TODO: Do this elsewhere? De-spaghetti everything later...
            wl_seat_add_listener(globals->seat, &seat_listener, state);
            fprintf(stderr, "added seat listener.\n");
        }
    } else if (_INTERFACE_IS(wl_shm_interface)) {
        globals->shm         = wl_registry_bind(registry, name, &wl_shm_interface, version);
    } else if (_INTERFACE_IS(zwlr_layer_shell_v1_interface)) {
        // XXX: version 4 cus nixpkgs
        globals->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
    } else if (_INTERFACE_IS(wp_cursor_shape_manager_v1_interface)) {
        // sway only has version 1 at the time of writing.
        globals->cursor_shape_manager = wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1);
    } else if (_INTERFACE_IS(wl_output_interface)) {
        if (globals->output != NULL) {
            // TODO: wl_list of outputs
            fprintf(stderr, "Ignoring additional wl_output global.\n");
        } else {
            fprintf(stderr, "Adding output... ");
            globals->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
            // TODO: Do this elsewhere? De-spaghetti everything later...
            wl_output_add_listener(globals->output, &output_listener, state);
            fprintf(stderr, "added output listener.\n");
        }
    } else if (_INTERFACE_IS(ext_output_image_capture_source_manager_v1_interface)) {
        globals->output_image_capture_source_manager = wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    } else if (_INTERFACE_IS(ext_image_copy_capture_manager_v1_interface)) {
        globals->image_copy_capture_manager = wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
    }

    #undef _EVENT_INTERFACE_IS
}

struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = NULL,
};
