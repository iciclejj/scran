#ifndef SCRAN_CAPTURE_H
#define SCRAN_CAPTURE_H

#include <assert.h>
#include <stdbool.h>
#include <time.h>

#include <libavutil/rational.h>

#include "ext-image-copy-capture-v1.h"
#include "state.h"
#include "util/blend2d.h"


#define NSEC_PER_SEC 1000000000
#define NSEC_PER_MS  1000000
#define NSEC_PER_US  1000
#define MS_PER_SEC   1000
#define MILLIHZ_PER_HZ 1000
#define BITS_PER_MEGABIT 1000000

#define IMAGE_CAPTURE_OUTPUT_BLFORMAT_DEFAULT BL_FORMAT_PRGB32
#define IMAGE_CAPTURE_OUTPUT_BLIMAGECODEC_NAME_DEFAULT "PNG"
#define IMAGE_CAPTURE_OUTPUT_FILE_EXTENSION_DEFAULT ".png"

enum {
    SCRAN_AV_FORMAT_STREAM_IDX_VIDEO,
    SCRAN_AV_FORMAT_STREAM_IDX_AUDIO,
};


void capture_session_init(struct capture_session *session, struct ext_image_capture_source_v1 *source);

void capture_update_selection(struct scran_output *st_output, BLBoxI selection_ctx_box_px);
void end_fullscreen_capture(struct scran_output *st_output);

void video_capture_write_video_packet(struct capture_frame_context *frame_ctx, AVPacket *pkt);
bool video_capture_start(struct scran_output *st_output);
bool video_capture_start_fullscreen(struct scran_output *st_output);
// Call video_capture_request_stop() to initiate graceful finish from arbitrary
// locations, rather than calling video_capture_finish() directly.
void video_capture_request_stop(struct scran_output *st_output);
void video_capture_finish(struct scran_output *st_output);
struct ext_image_copy_capture_frame_v1 * video_capture_create_frame(struct scran_output_capture *capture);
void video_capture_write_audio_packet(struct capture_frame_context *frame_ctx, AVPacket *av_packet);
void video_capture_destroy_ffmpeg(struct scran_output *st_output);

bool image_capture_start(struct scran_output *st_output, bool exit_after_capture);
bool image_capture_start_fullscreen(struct scran_output *st_output, bool exit_after_capture);
void image_capture_request_frame(struct scran_output *st_output, struct ext_image_copy_capture_frame_v1_listener *listener, struct ext_image_copy_capture_session_v1 *session, struct wl_buffer *buffer, int32_t buffer_width_px, int32_t buffer_height_px);


// HACK
//
// Client-requested *capture-buffer* damage does *not* necessarily trigger a new
// ::ready from the compositor, if no actual damage has occurred. Sway, for
// example, ignores it, and forces us to wait for an indefinite amount of time
// for the capture to end, if no movement has happened on screen.
//
// The optimal solution might be to handle this internally anyways, either
// manually duplicating it or (in the case of video recording) changing the
// frame duration retroactively.
//
// For now, just force/fake some *compositor/surface-buffer* damage to
// effectively force the compositor to send us another capture frame...
static inline void
capture_force_next_frame(
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

static inline void
video_capture_grow_tracked_damage(
    struct capture_frame_context *frame_ctx,
    int32_t x, int32_t y, int32_t w, int32_t h
) {
    // TODO: Keep multiple rects to represent the union of damage slightly more
    // accurately. Probably at least 2 is worthwhile, to be able to separately
    // track at least 1 window + 1 cursor.

    BLBoxI incoming_damage = blrecti_to_blboxi( (BLRectI){ x, y, w, h } );
    BLBoxI tracked_damage  = frame_ctx->capture_buffer_damage_area_px;

    if (blboxi_is_empty(tracked_damage)) {
        frame_ctx->capture_buffer_damage_area_px = incoming_damage;
    } else {
        frame_ctx->capture_buffer_damage_area_px = blboxi_bounding_box(incoming_damage, tracked_damage);
    }
}

static inline void
video_capture_damage_buffer(
    struct capture_frame_context *frame_ctx,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x, int32_t y, int32_t w, int32_t h
) {
    ext_image_copy_capture_frame_v1_damage_buffer(frame, x, y, w, h);
    video_capture_grow_tracked_damage(frame_ctx, x, y, w, h);
}

static inline uint8_t *
capture_get_area_start_address(
    struct capture_frame_context *frame_ctx,
    const struct capture_session *session,
    BLBoxI capture_buffer_area_px
) {
    return frame_ctx->scran_wl_buffer.data
         + session->pixel_stride * capture_buffer_area_px.y0 * session->source_dimensions_px.x
         + session->pixel_stride * capture_buffer_area_px.x0;
}

static inline BLBoxI
capture_get_selection_as_capture_buffer_area_px(
    const struct capture_frame_context *frame_ctx,
    const struct capture_session *session
) {
    const BLPointI source_dimensions_px = session->source_dimensions_px;

    assert(source_dimensions_px.x > 0);
    assert(source_dimensions_px.y > 0);

    const BLBoxI capture_buffer_area_px = blboxi_get_reverse_transform(
        frame_ctx->selection_ctx_box_px,
        source_dimensions_px.x,
        source_dimensions_px.y,
        frame_ctx->source_transform
    );

    assert(capture_buffer_area_px.x0 >= 0);
    assert(capture_buffer_area_px.y0 >= 0);
    assert(capture_buffer_area_px.x1 <= source_dimensions_px.x);
    assert(capture_buffer_area_px.y1 <= source_dimensions_px.y);

    return capture_buffer_area_px;
}

static inline int64_t
capture_clock_gettime_nsec() {
    // image-copy-capture protocol guarantees we get presentation time based
    // on system monotonic time.
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // XXX: Will overflow at tv_sec > ~584.9 years...
    return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}


#endif
