#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include "state.h"
#include "event-handlers.h"
#include "lib_interop.h"
#include "capture.h"

#include "print.h"

void
dispatch_capture_event_loop(struct client_state_output *st_output)
{
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            st_output->capture.session
        );
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener, st_output);
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        st_output->capture.frame_ctx.st_buffer.buffer
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}

bool
start_capture(struct client_state_output *st_output)
{
    // TODO: Assert instead?
    if (st_output->capture.frame_ctx.capturing) {
        DEBUG("Already capturing...\n");
        return false;
    }
    // TODO: Assert ffmpeg installed
    //          TODO: Probably use libav manually and don't launch ffmpeg
    // XXX: - Needs better asssert? Intent: make sure selection is complete and valid
    //      Most of this function is probably temporary anyways
    assert(
        st_output->selection.selection_state == SELECTION_COMPLETE
     || st_output->selection.selection_state == SELECTION_REBASING
     && st_output->selection.bl.box.x1
     && st_output->selection.bl.box.y1
    );

    // XXX: Double-check whether appropriate char-array sizes
    //      Also maybe clean up and/or optimize some of this filename stuff
    char ffmpeg_command[256];
    char time_now_str[64];
    time_t time_now = time(NULL);
    struct tm *tm_now = localtime(&time_now);
    const int width = st_output->capture.frame_ctx.capture_area.x1 - st_output->capture.frame_ctx.capture_area.x0;
    const int height = st_output->capture.frame_ctx.capture_area.y1 - st_output->capture.frame_ctx.capture_area.y0;
    strftime(time_now_str, sizeof(time_now_str), "%Y%m%d-%H%M%S", tm_now);
    snprintf(ffmpeg_command, 256,
        // XXX: Using -v quiet to suppress output and broken newline at end.
        //          TODO: Find better solution that still gives some logging
        "ffmpeg -v quiet -f rawvideo -video_size %dx%d -pix_fmt %s -i -"
            " test-capture_%s.mp4",
        width,
        height,
        wl_shm_format_to_ffmpeg_cli_str(st_output->capture.shm_format),
        time_now_str
    );
    DEBUG("FFMPEG COMMAND: `%s`\n", ffmpeg_command);

    st_output->capture.frame_ctx.capturing = true;
    st_output->capture.frame_ctx.ffmpeg = popen(ffmpeg_command, "w");
    st_output->capture.frame_ctx.ffmpeg_fd = fileno(st_output->capture.frame_ctx.ffmpeg);


    // Get initial frame. Subsequent capture requests happen within frame::ready
    //     Similar to the wl_surface callback event loop
    dispatch_capture_event_loop(st_output);

    return true;
}
