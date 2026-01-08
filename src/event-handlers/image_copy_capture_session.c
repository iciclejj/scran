#include <stdio.h>
#include <assert.h>

#include <wayland-client.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "wayland-event-handlers.h"

static void
handle_image_copy_capture_session_buffer_size(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t width,
    uint32_t height
) {
    struct client_state_output *st_output = data;

    // XXX TODO: Fix this once we have multiple monitor support 
    //           and/or logical output geomtry support (e.g. xdg_output)
    //           Also use 
    assert(st_output->mode.width_px == width);
    assert(st_output->mode.height_px == height);
    st_output->capture.source_width_px = width;
    st_output->capture.source_height_px = height;
}

static void
handle_image_copy_capture_session_shm_format(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t shm_format
) {
    struct client_state_output *st_output = data;

    fprintf(stderr, "session::shm_format received: %x... ", shm_format);

    // List of formats we want to support.
    // TODO: Add more formats and logic for handling them
    if (!st_output->capture.shm_format_is_selected
        &&
        (shm_format == WL_SHM_FORMAT_ARGB8888
         || shm_format == WL_SHM_FORMAT_XRGB8888
         || shm_format == WL_SHM_FORMAT_XBGR8888
         || shm_format == WL_SHM_FORMAT_ABGR8888
        )
    ) {
        st_output->capture.shm_format = shm_format;
        st_output->capture.shm_format_is_selected = true;
        st_output->capture.pixel_stride = 4;
        fprintf(stderr, "format supported!\n");
    } else {
        fprintf(stderr, "format unsupported.\n");
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
    // TODO: Ensure correct formats (anything else?) after capture session listener dispatch/roundtrip
    //           Either in ::done OR in later code
    .done = noop,
    .stopped = handle_image_copy_capture_session_stopped,
};
