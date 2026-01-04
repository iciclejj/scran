#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "wayland-event-handlers.h"

// Why does image_copy_capture support dynamic transform, but not dynamic
// geometry/resolution entirely? Only because of buffer sizes?
static void
handle_image_copy_capture_frame_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct client_state *state = data;

    state->capture.transform = transform;
}

static void
handle_image_copy_capture_frame_damage(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    // XXX TODO IMPORTANT: Implement this and add flag to enable damage-based capture
}

static void
handle_image_copy_capture_frame_ready(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    struct client_state *state = data;

    // TODO: Is there any scenario where we wouldn't want to destroy this
    //       as soon as we enter ::ready?
    ext_image_copy_capture_frame_v1_destroy(frame);


    // XXX TODO: Implement capture!



    struct ext_image_copy_capture_frame_v1 *new_frame;
    new_frame = ext_image_copy_capture_session_v1_create_frame(
        state->capture.session
    );
    ext_image_copy_capture_frame_v1_add_listener(
        new_frame,
        &image_copy_capture_frame_listener,
        state
    );
    ext_image_copy_capture_frame_v1_attach_buffer(
        new_frame,
        state->capture.buffer.buffer
    );
    ext_image_copy_capture_frame_v1_damage_buffer(
        new_frame,
        0,
        0,
        state->capture.source_width_px,
        state->capture.source_height_px
    );
    ext_image_copy_capture_frame_v1_capture(new_frame);
}

struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = noop, // TODO: Is this useful?
    .ready = handle_image_copy_capture_frame_ready,
};
