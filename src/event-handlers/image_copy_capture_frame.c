#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "wayland-event-handlers.h"
#include "capture.h"

// Why does image_copy_capture support dynamic transform, but not dynamic
// geometry/resolution entirely? Only because of buffer sizes?
static void
handle_image_copy_capture_frame_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct client_state_output *st_output = data;

    st_output->capture.transform = transform;
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
    struct client_state_output *st_output = data;

    // XXX TODO IMPORTANT: Implement this and add flag to enable damage-based capture
}

static void
handle_image_copy_capture_frame_ready(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    struct client_state_output *st_output = data;

    // TODO: Don't capture the overlay. Especially since it seemingly doesn't
    //       update in sync with the capture (the selection edges are sliding
    //       into the capture frame when moving it around)

    // TODO: Is there any scenario where we wouldn't want to destroy this
    //       as soon as we enter ::ready?
    ext_image_copy_capture_frame_v1_destroy(frame);

    // TODO: Make sure buffer is destroyed
    if (!st_output->capture.capturing) {
        pclose(st_output->capture.ffmpeg);
        return;
    }

    // TODO:
    //   - Make sure "inverted/flipped over itself" selection (e.g. y1 crosses y0)
    //     is handled here and/or in selection logic
    //   - Either assert width/height isn't 0 (and enforce in selection logic)
    //     or handle it properly here
    //

    // XXX: Clean up this eyesore. Change names or something, idk.
    uint32_t pixel_stride      = st_output->capture.pixel_stride;
    uint32_t height            = st_output->capture.frame_height_px;
    uint32_t width             = st_output->capture.frame_width_px;
    uint32_t source_width      = st_output->capture.source_width_px;
    uint32_t x                 = st_output->capture.frame_x_px;
    uint32_t y                 = st_output->capture.frame_y_px;

    uint32_t row_bytes         = pixel_stride * width;
    char *addr =
        st_output->capture.buffer.data
      + pixel_stride * y * source_width
      + pixel_stride * x;

    // TODO: We should properly handle this once per output during output init
    assert(st_output->capture.frame_iovec_size >= height);
    for (int i = 0; i < height; ++i) {
        st_output->capture.frame_iovec[i].iov_base = addr;
        st_output->capture.frame_iovec[i].iov_len = row_bytes;
        addr += pixel_stride * source_width;
    }

    // TODO: Assumes row-major. Find out whether this is a safe assumption.
    //           Maybe depends on transform? Something else?
    int bytes_remaining = height;
    while (bytes_remaining > 0) {
        const ssize_t bytes_to_write = bytes_remaining < __IOV_MAX ?
                                       bytes_remaining : __IOV_MAX;
        const ssize_t offset = height - bytes_remaining;

        const ssize_t bytes_written = writev(
            st_output->capture.ffmpeg_fd,
            st_output->capture.frame_iovec + offset,
            bytes_to_write
        );

        if (bytes_written < bytes_to_write) {
            fprintf(stderr, "Failed writev() (%ld/%d bytes)\n", bytes_written, bytes_to_write);
            if (bytes_written == -1) {
                fprintf(stderr, "    Error: %s\n", strerror(errno));
            }
            return; // TODO: Ensure returning here is safe.
        };

        bytes_remaining -= bytes_to_write;
    }

    dispatch_capture_event_loop(st_output);
}

struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = noop, // TODO: Is this useful?
    .ready = handle_image_copy_capture_frame_ready,
};
