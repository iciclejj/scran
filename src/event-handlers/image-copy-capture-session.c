#include <assert.h>
#include <stdlib.h>

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
    struct capture_session *capture_session = data;

    capture_session->source_dimensions_px.x = width;
    capture_session->source_dimensions_px.y = height;
}


static void
handle_image_copy_capture_session_shm_format(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t shm_format
) {
    struct capture_session *capture_session = data;

    DEBUG("Capture session advertised shm format: %x\n", shm_format);

    // List of formats we want to support.
    // TODO: Add more formats and logic for handling them
    //       NOTE:
    //         If implementing HDR support, make sure that the freezeframe
    //         surface picks an advertized format that the compositor can
    //         actually display, since it is NOT necessarily the same as the
    //         formats that it lets us capture.
    if (capture_session->shm_format == SCRAN_SHM_FORMAT_UNSET
        &&
        (shm_format == WL_SHM_FORMAT_ARGB8888
         || shm_format == WL_SHM_FORMAT_XRGB8888
         || shm_format == WL_SHM_FORMAT_XBGR8888
         || shm_format == WL_SHM_FORMAT_ABGR8888
        )
    ) {
        capture_session->shm_format = shm_format;
        capture_session->pixel_stride = 4;
    }
}


static void
handle_image_copy_capture_session_stopped(
    void *data,
    struct ext_image_copy_capture_session_v1 *session
) {
    struct capture_session *capture_session = data;

    assert(capture_session->wl_session == session);
    ext_image_copy_capture_session_v1_destroy(session);
    capture_session->wl_session = NULL;

    // TODO: More graceful exit and/or attempt creating a new session
    eprintf("Error: Capture session stopped unexpectedly.\n");
    exit(EXIT_FAILURE);
}


static void
handle_image_copy_capture_session_dmabuf_device(
    void *data,
    struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session_v1,
    struct wl_array *device
) {
    // TODO
}


static void
handle_image_copy_capture_session_dmabuf_format(
    void *data,
    struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session_v1,
    uint32_t format,
    struct wl_array *modifiers
) {
    // TODO
}


static void
handle_image_copy_capture_session_done(
    void *data,
    struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session_v1
) {
    // TODO
}


struct ext_image_copy_capture_session_v1_listener image_copy_capture_session_listener = {
    .buffer_size = handle_image_copy_capture_session_buffer_size,
    .shm_format = handle_image_copy_capture_session_shm_format,
    .dmabuf_device = handle_image_copy_capture_session_dmabuf_device,
    .dmabuf_format = handle_image_copy_capture_session_dmabuf_format,
    .done = handle_image_copy_capture_session_done,
    .stopped = handle_image_copy_capture_session_stopped,
};
