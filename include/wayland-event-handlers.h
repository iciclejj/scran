#ifndef HANDLE_POINTER_H
#define HANDLE_POINTER_H

#include <wayland-client.h>

// TODO:
//   * Find out how to check whether a given event handler is required.
//   * Go through every listener and make sure we have all desired events handled

static void
noop(/* XXX: leave blank to swallow args. TODO: Remove this... */)
{
};

extern struct wl_pointer_listener pointer_listener;
extern struct wl_seat_listener seat_listener;
extern struct wl_registry_listener registry_listener;
extern struct zwlr_layer_surface_v1_listener layer_surface_listener;
extern struct wl_buffer_listener buffer_listener;
extern struct wl_callback_listener surface_frame_callback_listener;
extern struct wl_output_listener output_listener;
extern struct ext_image_copy_capture_session_v1_listener image_copy_capture_session_listener;
extern struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener;

#endif
