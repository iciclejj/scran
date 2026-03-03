#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"

#include "state.h"
#include "event-handlers.h"


bool
init_premem__capture(
    struct scran_output *st_output,
    struct scran_seat_datacontrol *st_datacontrol,
    struct scran_globals *globals
) {
    st_output->capture.source = ext_output_image_capture_source_manager_v1_create_source(
        globals->output_image_capture_source_manager,
        st_output->wl_output
    );

    st_output->capture.session = ext_image_copy_capture_manager_v1_create_session(
        globals->image_copy_capture_manager,
        st_output->capture.source,
        // TODO: Make this a scran arg option
        // TODO: Not a big deal, but cursor doesn't seem in sync with area
        //       movement.
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

    // TODO: Revisit which parts of video and image init to put here vs
    // start_capture/dispatch

    // Image capture
    bl_pixel_converter_init(&st_output->capture.frame_ctx.bl_pixel_converter);
    bl_image_init(&st_output->capture.frame_ctx.bl_img_captured);
    bl_image_codec_init(&st_output->capture.frame_ctx.bl_imgcodec);

    st_output->capture.frame_ctx.st_datacontrol = st_datacontrol;

    return true;
}

void
init_premem__capture__destroy(struct scran_output *st_output)
{
    ext_image_capture_source_v1_destroy(st_output->capture.source);
    ext_image_copy_capture_session_v1_destroy(st_output->capture.session);

    bl_pixel_converter_destroy(&st_output->capture.frame_ctx.bl_pixel_converter);
    bl_image_destroy(&st_output->capture.frame_ctx.bl_img_captured);
    bl_image_codec_destroy(&st_output->capture.frame_ctx.bl_imgcodec);
}

