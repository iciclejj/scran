#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "event-handlers.h"
#include "capture.h"
#include "init.h"
#include "print.h"


static void
handle_image_copy_capture_frame_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;

    // TODO: What is this transform representing?
    //           It is separate from output::geometry's transform.
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
    struct capture_frame_context *frame_ctx = data;

    // XXX TODO IMPORTANT: Implement this and add flag to enable damage-based capture
}

// TODO:
//  - Can we fully avoid capturing the overlay (beyond just 
//    making sure it's out of frame) ?
static void
handle_image_copy_capture_frame_ready(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    struct capture_frame_context *frame_ctx = data;

    ext_image_copy_capture_frame_v1_destroy(frame);

    if (!frame_ctx->capturing) {
        pclose(frame_ctx->ffmpeg);
        return;
    }

    // TODO:
    //   - Make sure "inverted/flipped over itself" selection (e.g. y1 crosses y0)
    //     is handled here and/or in selection logic
    //   - Either assert width/height isn't 0 (and enforce in selection logic)
    //     or handle it properly here
    //

    uint32_t pixel_stride      = frame_ctx->pixel_stride;
    uint32_t source_width      = frame_ctx->source_width_px;
    uint32_t width             = frame_ctx->capture_area.x1 - frame_ctx->capture_area.x0;
    uint32_t height            = frame_ctx->capture_area.y1 - frame_ctx->capture_area.y0;
    uint32_t x                 = frame_ctx->capture_area.x0;
    uint32_t y                 = frame_ctx->capture_area.y0;
    uint32_t row_bytes         = pixel_stride * width;

    char *addr =
        frame_ctx->st_buffer.data
      + pixel_stride * y * source_width
      + pixel_stride * x;

    // TODO: Can we still do this assert somehow?
    // assert(GET_CAPTURE_IOV_SIZE((*st_output)) >= height);
    for (int i = 0; i < height; ++i) {
        frame_ctx->frame_iovec[i].iov_base = addr;
        frame_ctx->frame_iovec[i].iov_len = row_bytes;
        addr += pixel_stride * source_width;
    }

    int bytes_remaining = height;
    while (bytes_remaining > 0) {
        const ssize_t bytes_to_write = bytes_remaining < __IOV_MAX ?
                                       bytes_remaining : __IOV_MAX;
        const ssize_t offset = height - bytes_remaining;

        const ssize_t bytes_written = writev(
            frame_ctx->ffmpeg_fd,
            frame_ctx->frame_iovec + offset,
            bytes_to_write
        );

        if (bytes_written < bytes_to_write) {
            DEBUG("Failed writev() (%ld/%ld bytes)\n", bytes_written, bytes_to_write);
            if (bytes_written == -1) {
                DEBUG("    Error: %s\n", strerror(errno));
            }
            return; // TODO: Ensure returning here is safe.
        };

        bytes_remaining -= bytes_to_write;
    }

    dispatch_capture_event_loop(frame_ctx);
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = noop, // TODO: Is this useful?
    .ready = handle_image_copy_capture_frame_ready,
};
