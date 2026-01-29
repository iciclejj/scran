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

// XXX: Semi-arbitrary value (highest built-in AVCodecDescriptor.name in
// libavcodec atm. is 18, excl. null-terminator).
#define CAPTURE_OUTPUT_FILE_EXTENSION_MAX 20
// XXX TODO: Probably make a name based on program name, once we have one.
#define CAPTURE_OUTPUT_FILENAME_MAX NAME_MAX
#define CAPTURE_OUTPUT_FILEPATH_MAX PATH_MAX
#define CAPTURE_OUTPUT_DIRPATH_MAX (PATH_MAX - CAPTURE_OUTPUT_FILENAME_MAX)

#define CAPTURE_OUTPUT_DEFAULT_DIRPATH "/tmp/scran-capture"

void create_timestamped_filename(char filename_ret[CAPTURE_OUTPUT_FILENAME_MAX], const char file_extension[CAPTURE_OUTPUT_FILE_EXTENSION_MAX]);

bool start_video_capture(struct scran_output *st_output);
void dispatch_video_capture_event_loop(struct capture_frame_context *frame_ctx);

bool start_image_capture(struct scran_output *st_output);
void dispatch_image_capture_event(struct scran_output_capture *capture);


#endif
