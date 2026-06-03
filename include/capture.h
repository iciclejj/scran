#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>

#include <libavutil/rational.h>

#include "state.h"


#define NSEC_PER_SEC 1000000000
#define MILLIHZ_PER_HZ 1000
#define BITS_PER_MEGABIT 1000000

#define AV_FORMAT_STREAM_IDX_VIDEO 0
#define AV_FORMAT_STREAM_IDX_AUDIO 1

#define CAPTURE_IMAGE_OUTPUT_BLFORMAT_DEFAULT BL_FORMAT_PRGB32
#define CAPTURE_IMAGE_OUTPUT_BLIMAGECODEC_NAME_DEFAULT "PNG"
#define CAPTURE_IMAGE_OUTPUT_FILE_EXTENSION_DEFAULT ".png"


uint8_t *get_capture_area_start_address(struct capture_frame_context *frame_ctx);
void update_capture_area_with_selection(struct scran_output *st_output, BLBoxI selection_box);

void write_video_frame(struct capture_frame_context *frame_ctx, AVPacket *pkt);
void write_audio_packet(struct capture_frame_context *frame_ctx, AVPacket *av_packet);
bool request_video_capture(struct scran_output *st_output);
 void end_video_capture(struct scran_output *st_output);
void request_video_capture_frame(struct capture_frame_context *frame_ctx, int32_t damage_x, int32_t damage_y, int32_t damage_w, int32_t damage_h);
 void request_end_video_capture(struct scran_output *st_output);
void destroy_ffmpeg(struct scran_output *st_output);

bool request_image_capture(struct scran_output *st_output);
void dispatch_image_capture_event(struct scran_output *st_output);


// HACK
//
// Client-requested damage does *not* necessarily trigger a new ::ready from
// the compositor, if no actual damage has occurred. Sway, for example, ignores
// it, and forces us to wait for an indefinite amount of time for the capture to
// end, if no movement has happened on screen.
//
// The optimal solution might be to handle this internally anyways, either
// manually duplicating it or (in the case of video recording) changing the
// frame duration retroactively.
//
// For now, just force/fake some compositor damage to effectively force the
// compositor to send us another capture frame...
static inline void
force_compositor_output_damage_for_capture(
    struct scran_output *st_output
) {
    // Simply doing an empty commit, without damage, seems enough, but we'll
    // explicitly damage it just for good measure...
    //
    // TODO: This should maybe be made more robust, but this seems to work even if
    // it has a transparent buffer attached, at least on Hyprland.
    wl_surface_damage_buffer(st_output->selection_surface.surface.wl_surface, 0, 0, 1, 1);
    wl_surface_commit(st_output->selection_surface.surface.wl_surface);
}


#endif
