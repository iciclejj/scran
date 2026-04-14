#ifndef HANDLE_POINTER_H
#define HANDLE_POINTER_H

#include <wayland-client.h>

#include "state.h"

// TODO:
//   - Find out how to check whether a given event handler is required.
//      - I.e. some event handlers are not useful to us, but required by the
//        wayland server.
//   - Go through every listener and make sure we have all desired events handled
//   - Create add_listener wrappers with typed `data` args

enum scran_common_surface_update_handler_result {
    SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UNCHANGED,
    SCRAN_COMMON_SURFACE_UPDATE_HANDLER_UPDATED,
};

extern struct wl_pointer_listener pointer_listener;
extern struct zwlr_layer_surface_v1_listener layer_surface_listener__selection;
extern struct wl_buffer_listener selectionSurface_buffer_listener;
extern struct wl_buffer_listener capture_buffer_listener;
extern struct wl_callback_listener selection_surface_frame_callback_listener;
extern struct wl_output_listener output_listener;
extern struct ext_image_copy_capture_session_v1_listener image_copy_capture_session_listener;
extern struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__video_capture;
extern struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__image_capture;
extern struct ext_data_control_source_v1_listener data_control_source_listener;
extern struct zxdg_output_v1_listener xdg_output_listener;
extern struct wp_presentation_feedback_listener presentation_feedback_listener__selection;
extern struct wp_fractional_scale_v1_listener fractional_scale_listener__selection;
extern struct zcosmic_output_head_v1_listener cosmic_output_head_listener;
extern struct zwlr_output_head_v1_listener wlr_output_head_listener;
extern struct zwlr_output_manager_v1_listener wlr_output_manager_listener;

extern struct wl_registry_listener registry_listener;
 void registry_listener__destroy(struct scran *state);
extern struct wl_seat_listener seat_listener;
 void seat_listener__destroy(struct scran_seat *seat);
extern struct wl_keyboard_listener keyboard_listener;
 void keyboard_listener_destroy(struct scran_seat *st_seat);

#endif
