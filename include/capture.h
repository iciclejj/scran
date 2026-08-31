#ifndef SCRAN_CAPTURE_H
#define SCRAN_CAPTURE_H

#include <assert.h>
#include <stdbool.h>
#include <time.h>

#include <libavutil/rational.h>

#include "ext-image-copy-capture-v1.h"

#include "selection.h"
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

struct capture_buffer_area_context {
    const uint8_t *area_start_address;
    BLBoxI area_px;
    uint32_t source_row_bytes;
};

void capture_session_init(struct capture_session *session, struct ext_image_capture_source_v1 *source);

void capture_update_selection(struct scran_output *st_output, BLBoxI selection_ctx_box_px);

enum scran_capture_frame_consumers capture_fullscreen_dispatch_pending_consumers(struct scran_output *st_output, enum scran_capture_frame_consumers consumers);
enum scran_capture_frame_consumers capture_fullscreen_start(struct scran_output *st_output, enum scran_capture_frame_consumers consumers);
void capture_fullscreen_end(struct scran_output *st_output, enum scran_capture_frame_consumers consumers);

bool capture_request_frame(struct capture_session *session, enum scran_capture_frame_consumers consumer, const BLRectI *buffer_damage);

typedef void capture_video_write_packet_fn(
    struct scran_output *,
    AVPacket *pkt
);
bool capture_video_init_writers(struct scran_output *st_output, const BLPointI dimensions);
 void capture_video_destroy_video_writer(struct scran_output *st_output);
 void capture_video_destroy_audio_writer(struct scran_output *st_output);
bool capture_video_drain_writer(struct scran_output *st_output, AVCodecContext *codec_ctx, AVPacket *packet, capture_video_write_packet_fn write_packet_fn, const char *stream_name);
void capture_video_write_video_packet(struct scran_output *output, AVPacket *pkt);
void capture_video_write_audio_packet(struct scran_output *st_output, AVPacket *av_packet);
bool capture_video_write_video_frame(struct scran_output *output, struct capture_frame_context *frame_ctx, const struct capture_session_context *session, const struct capture_buffer_area_context *buffer_area_ctx);
void capture_image_write_image(struct scran_output *output, const struct capture_session_context *session, const struct capture_frame_context *frame_ctx, const struct capture_buffer_area_context *buffer_area_ctx);

bool capture_video_start(struct scran_output *st_output);
bool capture_video_start_fullscreen(struct scran_output *st_output);
// Call video_capture_request_stop() to initiate graceful finish from arbitrary
// locations, rather than calling video_capture_finish() directly.
void capture_video_request_stop(struct scran_output *st_output);
void capture_video_finish(struct scran_output *st_output);

bool capture_image_start(struct scran_output *st_output, bool exit_after_capture);
 void capture_image_finish(struct scran_output *output);
bool capture_image_start_fullscreen(struct scran_output *st_output, bool exit_after_capture);


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
    selection_do_some_damage(st_output);
}

static inline void
capture_request_frame_forced(
    struct capture_session *session,
    enum scran_capture_frame_consumers consumer,
    const BLRectI *damage
) {
    capture_request_frame(session, consumer, damage);

    // Some compositors (like Hyprland on rapid consecutive freezeframe refreshes)
    // may wait indefinitely for the next capture frame, if no damage is detected.
    //
    // Mainly needed for freezeframe/hide_selection_surface_then() captures.
    capture_force_next_frame(session->frame_ctx.output);
}

static inline void
capture_grow_tracked_damage(
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
capture_damage_buffer(
    struct capture_frame_context *frame_ctx,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x, int32_t y, int32_t w, int32_t h
) {
    ext_image_copy_capture_frame_v1_damage_buffer(frame, x, y, w, h);
    capture_grow_tracked_damage(frame_ctx, x, y, w, h);
}

static inline uint8_t *
capture_get_area_start_address(
    const struct capture_session_context *session,
    const struct capture_frame_context *frame_ctx,
    const BLBoxI *capture_buffer_area_px
) {
    return frame_ctx->scran_wl_buffer.data
         + session->pixel_stride * capture_buffer_area_px->y0 * session->source_dimensions_px.x
         + session->pixel_stride * capture_buffer_area_px->x0;
}

static inline BLBoxI
capture_get_selection_as_capture_buffer_area_px(
    const struct capture_session_context *session,
    const struct capture_frame_context *frame_ctx,
    const BLBoxI selection
) {
    const BLPointI source_dimensions_px = session->source_dimensions_px;

    assert(source_dimensions_px.x > 0);
    assert(source_dimensions_px.y > 0);

    const BLBoxI capture_buffer_area_px = blboxi_get_reverse_transform(
        selection,
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
