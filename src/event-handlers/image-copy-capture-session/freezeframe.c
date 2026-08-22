#include <assert.h>
#include <stdlib.h>

#include <wayland-client.h>

#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "event-handlers.h"
#include "print.h"


static void
handle_image_copy_capture_session_buffer_size__freezeframe(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t width,
    uint32_t height
) {
    struct scran_output *st_output = data;

    st_output->freezeframe.source_width_px = width;
    st_output->freezeframe.source_height_px = height;
}


static void
handle_image_copy_capture_session_shm_format__freezeframe(
    void *data,
    struct ext_image_copy_capture_session_v1 *session,
    uint32_t shm_format
) {
    struct scran_output *st_output = data;

    DEBUG("Freezeframe capture session advertised shm format: %x\n", shm_format);

    // List of formats we want to support.
    // TODO: Add more formats and logic for handling them
    if (st_output->freezeframe.session.shm_format == SCRAN_SHM_FORMAT_UNSET
        &&
        (shm_format == WL_SHM_FORMAT_ARGB8888
         || shm_format == WL_SHM_FORMAT_XRGB8888
         || shm_format == WL_SHM_FORMAT_XBGR8888
         || shm_format == WL_SHM_FORMAT_ABGR8888
        )
    ) {
        // We still need to filter out some formats for freezeframe, since e.g
        // cosmic will advertise 10-bit formats that the compositor is not
        // necessarily able to present.
        st_output->freezeframe.session.shm_format = shm_format;
    }
}


static void
handle_image_copy_capture_session_stopped__freezeframe(
    void *data,
    struct ext_image_copy_capture_session_v1 *session
) {
    struct scran_output *st_output = data;

    ext_image_copy_capture_session_v1_destroy(st_output->freezeframe.session.wl_session);
    st_output->freezeframe.session.wl_session = NULL;

    // XXX TODO: Clean up and disable freezeframe, and show message in notification.
    eprintf("Error: Capture session stopped unexpectedly.\n");
    exit(EXIT_FAILURE);
}


static void handle_image_copy_capture_session_dmabuf_device__freezeframe( void *data, struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session_v1, struct wl_array *device) { }
static void handle_image_copy_capture_session_dmabuf_format__freezeframe( void *data, struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session_v1, uint32_t format, struct wl_array *modifiers) { }
static void handle_image_copy_capture_session_done__freezeframe(void *data, struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session_v1) { }

struct ext_image_copy_capture_session_v1_listener image_copy_capture_session_listener__freezeframe = {
    .buffer_size = handle_image_copy_capture_session_buffer_size__freezeframe,
    .shm_format = handle_image_copy_capture_session_shm_format__freezeframe,
    .dmabuf_device = handle_image_copy_capture_session_dmabuf_device__freezeframe,
    .dmabuf_format = handle_image_copy_capture_session_dmabuf_format__freezeframe,
    .done = handle_image_copy_capture_session_done__freezeframe,
    .stopped = handle_image_copy_capture_session_stopped__freezeframe,
};
