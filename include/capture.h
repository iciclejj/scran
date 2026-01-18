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

bool start_capture(struct client_state_output *st_output);
void dispatch_capture_event_loop(struct capture_frame_context *frame_ctx);

#endif
