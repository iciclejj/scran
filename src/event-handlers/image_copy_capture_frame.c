#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

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

    // TODO: Don't capture the overlay. Especially since it seemingly doesn't
    //       update in sync with the capture (the selection edges are sliding
    //       into the capture frame when moving it around)

    // TODO: Is there any scenario where we wouldn't want to destroy this
    //       as soon as we enter ::ready?
    ext_image_copy_capture_frame_v1_destroy(frame);

    // TODO: Make sure buffer is destroyed
    if (!state->capture.capturing) {
        pclose(state->capture.ffmpeg);
        return;
    }

    // TODO:
    //   - Make sure "inverted/flipped over itself" selection (e.g. y1 crosses y0)
    //     is handled here and/or in selection logic
    //   - Either assert width/height isn't 0 (and enforce in selection logic)
    //     or handle it properly here
    //

    // XXX: Clean up this eyesore. Change names or something, idk.
    uint32_t pixel_stride      = state->capture.pixel_stride;
    uint32_t height            = state->capture.frame_height_px;
    uint32_t width             = state->capture.frame_width_px;
    uint32_t source_width      = state->capture.source_width_px;
    uint32_t x                 = state->capture.frame_x_px;
    uint32_t y                 = state->capture.frame_y_px;

    uint32_t row_bytes         = pixel_stride * width;
    char *addr =
        state->capture.buffer.data
      + pixel_stride * y * source_width
      + pixel_stride * x;

    for (int i = 0; i < height; ++i) {
        state->capture.frame_iovec[i].iov_base = addr;
        state->capture.frame_iovec[i].iov_len = row_bytes;
        addr += pixel_stride * source_width;
    }

    // TODO: Assumes row-major. Find out whether this is a safe assumption.
    //           Maybe depends on transform? Something else?
    if (-1 == writev(state->capture.ffmpeg_fd, state->capture.frame_iovec, height)) {
        fprintf(stderr, "Failed writev(). Error: %s\n", strerror(errno));
    };

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
