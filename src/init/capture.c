#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "init.h"
#include "capture.h"
#include "event-handlers.h"


void
capture_session_init(
    struct capture_session *session,
    struct ext_image_capture_source_v1 *source
) {
    assert(source);

    struct capture_session_context *session_ctx = &session->session_ctx;

    session_ctx->wl_session = ext_image_copy_capture_manager_v1_create_session(
        g_state.globals.image_copy_capture_manager,
        source,
        g_state.options.disable_cursor_capture ? 0 : EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
    );
    // XXX: Maybe there's a nicer way to do this or to properly assert this
    //      initialization in the listener somewhere?
    session_ctx->shm_format   = SCRAN_SHM_FORMAT_UNSET;
    session_ctx->pixel_stride = 0;

    ext_image_copy_capture_session_v1_add_listener(
        session_ctx->wl_session,
        &image_copy_capture_session_listener,
        session_ctx
    );
}

bool
init_premem__capture(
    struct scran_output *st_output,
    struct scran_globals *globals
) {
    st_output->capture.source = ext_output_image_capture_source_manager_v1_create_source(
        globals->output_image_capture_source_manager,
        st_output->wl_output
    );
    capture_session_init(
        &st_output->capture.session,
        st_output->capture.source
    );
    st_output->capture.session.frame_ctx.output = st_output;

    // TODO: Revisit which parts of video and image init to put here vs
    // start_capture/dispatch

    // Image capture
    bl_image_init(&st_output->capture.bl_img_captured);
    bl_image_codec_init(&st_output->capture.bl_imgcodec);

    return true;
}

void
init_premem__capture__destroy(struct scran_output *st_output)
{
    ext_image_capture_source_v1_destroy(st_output->capture.source);
    if (st_output->capture.session.session_ctx.wl_session) {
        ext_image_copy_capture_session_v1_destroy(st_output->capture.session.session_ctx.wl_session);
    }

    bl_image_destroy(&st_output->capture.bl_img_captured);
    bl_image_codec_destroy(&st_output->capture.bl_imgcodec);
}
