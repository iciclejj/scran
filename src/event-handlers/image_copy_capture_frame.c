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
        goto end_capture;
    }

    // TODO:
    //   - Make sure "inverted/flipped over itself" selection (e.g. y1 crosses y0)
    //     is handled here and/or in selection logic
    //   - Either assert width/height isn't 0 (and enforce in selection logic)
    //     or handle it properly here
    //

    uint32_t area_width_px   = frame_ctx->capture_area_px.x1 - frame_ctx->capture_area_px.x0;
    uint32_t area_height_px  = frame_ctx->capture_area_px.y1 - frame_ctx->capture_area_px.y0;
    uint32_t area_row_bytes       = frame_ctx->pixel_stride * area_width_px;

    char *const area_start_addr =
        frame_ctx->st_buffer.data
      + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
      + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;

    // TODO: Can we still do this assert somehow?
    // assert(GET_CAPTURE_IOV_LEN((*st_output)) >= height_px);
    char *curr_addr = area_start_addr;
    for (int i = 0; i < area_height_px; ++i) {
        frame_ctx->frame_iovec[i].iov_base = curr_addr;
        frame_ctx->frame_iovec[i].iov_len = area_row_bytes;
        curr_addr += frame_ctx->pixel_stride * frame_ctx->source_width_px;
    }

    int rows_remaining = area_height_px;
    while (rows_remaining > 0) {
        const ssize_t rows_offset = area_height_px - rows_remaining;
        const ssize_t rows_to_write = rows_remaining < __IOV_MAX ?
                                      rows_remaining : __IOV_MAX;

        const ssize_t bytes_written = writev(
            frame_ctx->ffmpeg_fd,
            frame_ctx->frame_iovec + rows_offset,
            rows_to_write
        );

        // TODO: Handle partial writes
        if (bytes_written == -1) {
            eprintf("Failed writev() during capture\n");
            goto end_capture;
        };

        rows_remaining -= rows_to_write;
    }

    dispatch_capture_event_loop(frame_ctx);

    return;

end_capture:
    assert(frame_ctx->ffmpeg);
    pclose(frame_ctx->ffmpeg);
    return;
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = noop, // TODO: Is this useful?
    .ready = handle_image_copy_capture_frame_ready,
};
