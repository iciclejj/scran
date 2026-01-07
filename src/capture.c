#include <stdbool.h>
#include <assert.h>
#include <time.h>

#include "state.h"
#include "wayland-event-handlers.h"
#include "lib_interop.h"

bool
start_capture(struct client_state *state)
{
    // TODO: Assert instead?
    if (state->capture.capturing) {
        fprintf(stderr, "Already capturing...\n");
        return false;
    }
    // TODO: Assert ffmpeg installed
    //          TODO: Probably use libav manually and don't launch ffmpeg
    // XXX: - Needs better asssert? Intent: make sure selection is complete and valid
    //      Most of this function is probably temporary anyways
    assert(
        state->selection.selection_state == SELECTION_COMPLETE
     || state->selection.selection_state == SELECTION_REBASING
     && state->selection.bl.box.x1
     && state->selection.bl.box.y1
    );

    // XXX: Double-check whether appropriate char-array sizes
    //      Also maybe clean up and/or optimize some of this filename stuff
    char ffmpeg_command[256];
    char time_now_str[64];
    time_t time_now = time(NULL);
    struct tm *tm_now = localtime(&time_now);
    strftime(time_now_str, sizeof(time_now_str), "%Y%m%d-%H%M%S", tm_now);
    snprintf(ffmpeg_command, 256,
        // XXX: Using -v quiet to suppress output and broken newline at end.
        //          TODO: Find better solution that still gives some logging
        "ffmpeg -v quiet -f rawvideo -video_size %dx%d -pix_fmt %s -i -"
            " test-capture_%s.mp4",
        state->capture.frame_width_px,
        state->capture.frame_height_px,
        wl_shm_format_to_ffmpeg_cli_str(state->capture.shm_format),
        time_now_str
    );
    fprintf(stderr, "FFMPEG COMMAND: `%s`\n", ffmpeg_command);

    state->capture.capturing = true;
    state->capture.ffmpeg = popen(ffmpeg_command, "w");
    state->capture.ffmpeg_fd = fileno(state->capture.ffmpeg);


    // Get initial frame. Subsequent capture requests happen within frame::ready
    //     Similar to the wl_surface callback event loop

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            state->capture.session
        );
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener, state);
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        state->capture.buffer.buffer
    );
    ext_image_copy_capture_frame_v1_damage_buffer(
        frame,
        0,
        0,
        state->capture.source_width_px,
        state->capture.source_height_px
    );
    ext_image_copy_capture_frame_v1_capture(frame);

    return true;
}
