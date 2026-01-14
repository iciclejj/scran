#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "ext-image-copy-capture-v1.h"

#include "init.h"
#include "state.h"
#include "event-handlers.h"

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
    st_output->capture.frame_ctx.session = &st_output->capture.session;

    // XXX: Maybe there's a nicer way to do this or to properly assert this
    //      initialization in the listener somewhere?
    st_output->capture.shm_format = -1;
    ext_image_copy_capture_session_v1_add_listener(
        st_output->capture.session,
        &image_copy_capture_session_listener,
        st_output
    );

    return true;
}

