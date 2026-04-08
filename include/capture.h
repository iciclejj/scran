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

#define CAPTURE_IMAGE_OUTPUT_BLFORMAT_DEFAULT BL_FORMAT_PRGB32
#define CAPTURE_IMAGE_OUTPUT_BLIMAGECODEC_NAME_DEFAULT "PNG"
#define CAPTURE_IMAGE_OUTPUT_FILE_EXTENSION_DEFAULT ".png"


bool start_video_capture(struct scran_output *st_output);
void init_wl_capture_frame__video(struct capture_frame_context *frame_ctx);
void destroy_ffmpeg(struct scran_output *st_output);

bool start_image_capture(struct scran_output *st_output);
void dispatch_image_capture_event(struct scran_output_capture *capture);


#endif
