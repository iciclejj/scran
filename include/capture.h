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


uint8_t *get_capture_area_start_address(struct capture_frame_context *frame_ctx);
void update_capture_area_with_selection(struct scran_output *st_output, BLBoxI selection_box);

bool request_video_capture(struct scran_output *st_output);
 void end_video_capture(struct scran_output *st_output);
void request_video_capture_frame(struct capture_frame_context *frame_ctx, int32_t damage_x, int32_t damage_y, int32_t damage_w, int32_t damage_h);
 void request_end_video_capture(struct scran_output *st_output);
void destroy_ffmpeg(struct scran_output *st_output);

bool request_image_capture(struct scran_output *st_output);
void dispatch_image_capture_event(struct scran_output *st_output);


#endif
