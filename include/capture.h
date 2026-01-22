#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>
#include <libavutil/rational.h>

#include "state.h"


#define NSEC_PER_SEC 1000000000
#define MILLIHZ_PER_HZ 1000
#define BITS_PER_MEGABIT 1000000

#define AV_FORMAT_STREAM_IDX_VIDEO 0
// #define AV_FORMAT_STREAM_IDX_AUDIO 1

// XXX TODO: Probably make a name based on program name, once we have one.
#define CAPTURE_OUTPUT_FILENAME_MAX NAME_MAX
// XXX: Semi-arbitrary value (highest built-in AVCodecDescriptor.name in
// libavcodec atm. is 18, excl. null-terminator).
#define CAPTURE_OUTPUT_FILE_EXTENSION_MAX 20

bool start_video_capture(struct client_state_output *st_output);
void dispatch_video_capture_event_loop(struct capture_frame_context *frame_ctx);

void create_timestamped_filename(char filename_ret[CAPTURE_OUTPUT_FILENAME_MAX], char file_extension[CAPTURE_OUTPUT_FILE_EXTENSION_MAX]);

#endif
