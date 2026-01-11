#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "ext-image-copy-capture-v1.h"

#include "init.h"
#include "state.h"
#include "wayland-event-handlers.h"

bool
init_capture(
    struct client_state_output *st_output,
    struct client_state_globals *globals
) {
    st_output->capture.source = ext_output_image_capture_source_manager_v1_create_source(
        globals->output_image_capture_source_manager,
        st_output->wl_output
    );

    st_output->capture.session = ext_image_copy_capture_manager_v1_create_session(
        globals->image_copy_capture_manager,
        st_output->capture.source,
        // TODO: Make this optional
        EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
    );
    ext_image_copy_capture_session_v1_add_listener(
        st_output->capture.session,
        &image_copy_capture_session_listener,
        st_output
    );

    return true;
}

bool
init_image_copy_capture_shm_buffer(
    struct client_state_output *st_output,
    struct client_state_globals *globals
) {
    // TODO: Somehow assert session::shm_buffer has ran?
    if (!st_output->capture.shm_format_is_selected) {
        fprintf(stderr, "Failed to select shm_buffer format.\n");
        return false;
    }

    // Full output source buffer for now.
    // TODO: Revisit this after multi-output support.
    const ssize_t buf_size = GET_CAPTURE_BUF_SIZE((*st_output));
    const ssize_t shm_pool_size = buf_size;

    int shm_fd = shm_open_anon();

    // XXX: MEMORY ALLOC/FREE HERE
    if (-1 == ftruncate(shm_fd, shm_pool_size)) {
        fprintf(stderr, "Failed to resize shm file to %ld\n", shm_pool_size);
        close(shm_fd);
        return false;
    }

    // TODO: Use same pool as surface?
    st_output->capture.shm_pool = wl_shm_create_pool(
        globals->shm,
        shm_fd,
        shm_pool_size
    );

    st_output->capture.buffer.buffer = wl_shm_pool_create_buffer(
        st_output->capture.shm_pool,
        0,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->capture.pixel_stride * st_output->mode.width_px,
        st_output->capture.shm_format
    );

    // XXX: MEMORY ALLOC/FREE HERE
    st_output->capture.buffer.data = mmap(
        0, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0
    );

    // XXX: MEMORY ALLOC/FREE HERE
    close(shm_fd);
    wl_shm_pool_destroy(st_output->capture.shm_pool);

    if (st_output->capture.buffer.data == NULL) {
        return false;
    }

    return true;
}

void
destroy_capture_shm_buffers(struct client_state_output_capture *st_capture)
{
    // XXX: MEMORY ALLOC/FREE HERE
    wl_buffer_destroy(st_capture->buffer.buffer);
}

