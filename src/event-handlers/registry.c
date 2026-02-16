#include <assert.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "ext-data-control-v1.h"
#include "xdg-output-unstable-v1.h"

#include "state.h"
#include "event-handlers.h"
#include "print.h"

static void
registry_handle_global(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version // of interface
    )
{
    struct scran *state = data;
    struct scran_globals *globals = &state->globals;

    #define _INTERFACE_IS(desired) (strcmp(interface, desired.name) == 0)

    // TODO: Determine desired minimum versions.
    if (_INTERFACE_IS(wl_compositor_interface)) {
        globals->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
    } else if (_INTERFACE_IS(wl_seat_interface)) {
        if (globals->seat != NULL) {
            // TODO: wl_list of seats
            DEBUG("Ignoring additional wl_seat global.\n");
        } else {
            DEBUG("Adding seat... ");
            globals->seat    = wl_registry_bind(registry, name, &wl_seat_interface, version);
            // TODO: Do this elsewhere? De-spaghetti everything later...
            wl_seat_add_listener(globals->seat, &seat_listener, state);
            DEBUG("added seat listener.\n");
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
        if (state->n_outputs >= MAX_OUTPUTS) {
            DEBUG("Maximum output limit reached: %d\n", MAX_OUTPUTS);
            return;
        }
        // TODO: Handle adding/removing outputs during program runtime?

        struct scran_output *curr_output = &state->outputs[state->n_outputs];

        curr_output->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
        wl_output_add_listener(curr_output->wl_output, &output_listener, curr_output);

        ++state->n_outputs;
    } else if (_INTERFACE_IS(zxdg_output_manager_v1_interface)) {
        // TODO: Acutally implement this...
        globals->xdg_output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 3);
    } else if (_INTERFACE_IS(ext_output_image_capture_source_manager_v1_interface)) {
        globals->output_image_capture_source_manager = wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    } else if (_INTERFACE_IS(ext_image_copy_capture_manager_v1_interface)) {
        globals->image_copy_capture_manager = wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
    } else if (_INTERFACE_IS(ext_data_control_manager_v1_interface)) {
        globals->data_control_manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    }

    #undef _INTERFACE_IS
}

struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = NULL,
};

// TODO: Document the "destroy-function tree". =
//       TLDR: Recursively and (inversely?)chronologically create and call
//       *_destroy functions for any piece of code (e.g. listeners or functions
//       int other files) that triggers an init in need of later destruction.
//
//       Example:
//
//       init_premem_destroy<root_node> {
//           init_globals_destroy {
//               registry_listener_destroy {
//                   seat_listener_destroy {
//                       keyboard_listener_destroy {
//                           wl_*_destroy
//                           ...
//                       }
//
//                       wl_*_destroy
//                       ...
//                   }
//               }
//           }
//
//           wl_*_destroy
//           ...
//       }
//
// TODO: Well I guess I ended up basically documenting it already. Now put it
// somewhere nice
void
registry_listener__destroy(struct scran *state)
{
    const struct scran_globals *const globals = &state->globals;

    // TODO: Destroy properly per seat once multi-seat implemented
    seat_listener__destroy(&state->seat);

    // TODO: Is a roundtrip necessary?

    wl_compositor_destroy(globals->compositor);
    wl_seat_destroy(globals->seat);
    wl_shm_destroy(globals->shm);
    zwlr_layer_shell_v1_destroy(globals->layer_shell);
    wp_cursor_shape_manager_v1_destroy(globals->cursor_shape_manager);
    ext_output_image_capture_source_manager_v1_destroy(globals->output_image_capture_source_manager);
    ext_image_copy_capture_manager_v1_destroy(globals->image_copy_capture_manager);
    // TODO: Add remaining..?

    wl_registry_destroy(globals->registry);
}
