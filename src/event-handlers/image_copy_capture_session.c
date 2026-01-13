#include <stdio.h>
#include <assert.h>

#include <wayland-client.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "event-handlers.h"
#include "print.h"

static void
handle_image_copy_capture_session_buffer_size(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t width,
    uint32_t height
) {
    struct client_state_output *st_output = data;

    // This seemingly always hold true, so use output::mode w/h only.
    // TODO: See if xdg_output can somehow be used as backing for the session.
    //           As a foreign toplevel..?
    assert(st_output->mode.width_px == width);
    assert(st_output->mode.height_px == height);
}

static void
handle_image_copy_capture_session_shm_format(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t shm_format
) {
    struct client_state_output *st_output = data;

    DEBUG("session::shm_format received: %x... ", shm_format);

    // List of formats we want to support.
    // TODO: Add more formats and logic for handling them
    if (st_output->capture.shm_format == -1
        &&
        (shm_format == WL_SHM_FORMAT_ARGB8888
         || shm_format == WL_SHM_FORMAT_XRGB8888
         || shm_format == WL_SHM_FORMAT_XBGR8888
         || shm_format == WL_SHM_FORMAT_ABGR8888
        )
    ) {
        st_output->capture.shm_format = shm_format;
        st_output->capture.frame_ctx.pixel_stride = 4;
        DEBUG("format supported!\n");
    } else {
        DEBUG("format unsupported.\n");
    }
}

static void
handle_image_copy_capture_session_stopped(
    void *data,
    struct ext_image_copy_capture_session_v1 *session
) {
    struct client_state_output *st_output = data;

    ext_image_copy_capture_session_v1_destroy(st_output->capture.session);

    // TODO: Destroy frames, free shm etc.
}


struct ext_image_copy_capture_session_v1_listener image_copy_capture_session_listener = {
    .buffer_size = handle_image_copy_capture_session_buffer_size,
    .shm_format = handle_image_copy_capture_session_shm_format,
    .dmabuf_device = noop, // TODO
    .dmabuf_format = noop, // TODO
    .done = noop,
    .stopped = handle_image_copy_capture_session_stopped,
};
