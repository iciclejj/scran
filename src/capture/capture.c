/*
 * General capture orchestration goes here.
 * Writing/encoding-specifics goes into write/
 */
#include <stdbool.h>

#include <blend2d/blend2d.h>

#include "clipboard.h"
#include "dbus.h"
#include "freezeframe.h"
#include "pipewires.h"
#include "selection-surface.h"
#include "selection.h"
#include "state-util.h"
#include "state.h"
#include "capture.h"
#include "event-handlers.h"
#include "util/blend2d.h"


// `selection_ctx_box_px` has `scran_output_selectionContext.box_px` coordinate space!
void
capture_update_selection(struct scran_output *st_output, BLBoxI selection_ctx_box_px) {
    struct scran_output_capture *capture = &st_output->capture;

    bool size_changed =
           blboxi_width_abs_unsafe(capture->selection_ctx_box_px)  != blboxi_width_abs_unsafe(selection_ctx_box_px)
        || blboxi_height_abs_unsafe(capture->selection_ctx_box_px) != blboxi_height_abs_unsafe(selection_ctx_box_px);

    // Presentation feedback for an older selection-surface buffer can arrive
    // after video capture has frozen the selection size.
    if (st_output->selection_ctx.size_is_frozen && size_changed) {
        return;
    }

    capture->selection_ctx_box_px = selection_ctx_box_px;
}


bool
capture_request_frame(
    struct capture_session *session,
    enum scran_capture_frame_consumers consumer,
    const BLRectI *damage
) {
    struct capture_frame_context *frame_ctx = &session->frame_ctx;

    if (frame_ctx->frame) {
        frame_ctx->consumers |= consumer;
        return true;
    }

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(session->session_ctx.wl_session);

    ext_image_copy_capture_frame_v1_attach_buffer(frame, frame_ctx->scran_wl_buffer.wl_buffer);
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener, frame_ctx);

    // TODO: inline to remove branch
    if (damage != NULL) {
        capture_damage_buffer(frame_ctx, frame, damage->x, damage->y, damage->w, damage->h);
    }

    frame_ctx->frame     = frame;
    frame_ctx->consumers |= consumer;

    ext_image_copy_capture_frame_v1_capture(frame);

    return true;
}

static inline void capture_video_cancel_pending_fullscreen_capture(struct scran_output *output);

enum scran_capture_frame_consumers
capture_fullscreen_dispatch_pending_consumers(
    struct scran_output *st_output,
    enum scran_capture_frame_consumers pending
) {
    enum scran_capture_frame_consumers started = 0;
    st_output->capture.fullscreen_consumers |= pending;

    if (pending & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        if (capture_image_start(st_output, st_output->capture.exit_after_capture)) {
            started |= SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
        }
    }

    if (pending & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        st_output->capture.audio_disable_modifier_active = st_output->capture.fullscreen_video_pending_audio_disabled;
        st_output->capture.fullscreen_video_pending_audio_disabled = false;

        if (!g_state.exit_requested && capture_video_start(st_output)) {
            started |= SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
        } else {
            capture_video_cancel_pending_fullscreen_capture(st_output);
        }
    }

    if (pending & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        // TODO: Make freezeframe able to not set started?
        started |= SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME;
        freezeframe_capture_start_assume_callback_set(st_output);
    }

    enum scran_capture_frame_consumers failed = pending & ~started;
    if (failed) {
        capture_fullscreen_end(st_output, failed);
    }

    return started;
}

// TODO: returns added or dispatched consumers
enum scran_capture_frame_consumers
capture_fullscreen_start(
    struct scran_output *st_output,
    enum scran_capture_frame_consumers consumers
) {
    enum scran_capture_frame_consumers prev_consumers = st_output->capture.fullscreen_consumers;
    enum scran_capture_frame_consumers prev_pending   = st_output->capture.pending_fullscreen_consumers;
    enum scran_capture_frame_consumers new_consumers  = consumers & ~(prev_pending | prev_consumers);

    if (!new_consumers) {
        return 0;
    }

    // If we have live consumers, it means fullscreen-capture is already set up
    if (prev_consumers) {
        assert(!prev_pending);
        return capture_fullscreen_dispatch_pending_consumers(st_output, new_consumers);
    }

    st_output->capture.pending_fullscreen_consumers |= new_consumers;

    if (prev_pending) {
        return new_consumers;
    }

    // HACK: Prevent exit while fullscreen capture is starting, despite the actual
    // capture not having started yet. This adds a "fake" capture to the counter.
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    selection_surface_acquire_hide_then(st_output, &presentation_feedback_listener__transparent_selection_capture, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FULLSCREEN_HIDE);

    return new_consumers;
}

void
capture_fullscreen_end(
    struct scran_output *st_output,
    enum scran_capture_frame_consumers consumers
) {
    st_output->capture.fullscreen_consumers &= ~consumers;

    if (st_output->capture.pending_fullscreen_consumers ||
        st_output->capture.fullscreen_consumers
    ) {
        return;
    }

    // We don't want to flash a frame of selection/background dim if we're exiting anyways
    if (!st_output->capture.exit_after_capture) {
        selection_surface_release_hide(st_output, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FULLSCREEN_HIDE);
    }

    // HACK: See comment in start_fullscreen_capture().
    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


bool
capture_video_start(struct scran_output *st_output)
{
    const BLPointI source_dimensions_px = st_output->capture.session.session_ctx.source_dimensions_px;

    // TODO: Assert instead?
    if (capture_video_is_live(st_output)) {
        DEBUG("Already capturing...\n");
        return false;
    }

    selection_freeze_size(st_output);

    const bool fullscreen = st_output->capture.fullscreen_consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
    const BLPointI dimensions = fullscreen
        ? blboxi_get_dimensions(get_fullscreen_selection_box(st_output))
        : blboxi_get_dimensions(st_output->capture.selection_ctx_box_px);

    // TODO: Assert box is within output dimensions
    assert(dimensions.x && dimensions.y);

    if (g_state.options.output_to_stdout) {
        if (!scran_stdout_try_reserve(&st_output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO)) {
            scran_stdout_print_busy_message();
            goto capture_video_start_fail_1;
        }
    }

    if (!capture_video_init_writers(st_output, dimensions)) {
        eprintf("Error: Failed to initialize ffmpeg libraries.\n");
        // TODO: goto fail if this becomes more complicated
        goto capture_video_start_fail_2;
    }

    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_color(   SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_COLOR_KEYMAP_VIDEO_CAPTURE);
        scran_ui_textline_item_set_locked(  SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, true);
    }
    st_output->capture.pre_capture_selection_theme = st_output->selection_surface.theme;
    selection_surface_set_theme(st_output, SURFACE_THEME_VIDEO_CAPTURE);
    cursor_set_theme(st_output, SCRAN_CURSOR_THEME_VIDEO_CAPTURE);
    request_selection_surface_frame_callback(st_output);

    st_output->capture.video_presentation_time_nsec_start = capture_clock_gettime_nsec();

    // Get initial frame. Subsequent capture requests happen within
    // frame::ready, similar to the wl_surface callback event loop
    capture_request_frame_forced(
        &st_output->capture.session, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO,
        // Ensure the first frame is fully rendered
        &(BLRectI){ 0, 0, source_dimensions_px.x, source_dimensions_px.y }
    );


    if (st_output->capture.audio_active) {
        scran_pipewire_connect();
    }

    st_output->capture.video_stage = SCRAN_VIDEO_STAGE_CAPTURING;
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    return true;

capture_video_start_fail_2:
    scran_stdout_release(&st_output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO);
capture_video_start_fail_1:
    selection_unfreeze_size(st_output);
    return false;
}

bool
capture_video_start_fullscreen(struct scran_output *st_output)
{
    struct scran_output_capture *capture = &st_output->capture;

    // TODO: Reserve stdout already here, once we have better capture-state
    // tracking with e.g. an enum

    // TODO: Assert instead?
    if (capture_video_is_live(st_output)) {
        DEBUG("Already capturing...\n");
        return false;
    }

    bool prev_pending_audio_disabled = capture->fullscreen_video_pending_audio_disabled;

    // Must be set prior to capture_fullscreen_start(), since it will dispatch
    // the capture instantly when possible.
    capture->fullscreen_video_pending_audio_disabled = capture->audio_disable_modifier_active;
    capture->video_stage                             = SCRAN_VIDEO_STAGE_FULLSCREEN_START_PENDING;

    if (!capture_fullscreen_start(
            st_output,
            SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO)
    ) {
        capture->fullscreen_video_pending_audio_disabled = prev_pending_audio_disabled;
        capture->video_stage                             = SCRAN_VIDEO_STAGE_NONE;
        return false;
    }

    // Freeze already here to block entering SELECTION_INITIALIZING
    selection_freeze_size(st_output);

    return true;
}

static inline void
capture_video_cancel_pending_fullscreen_capture(struct scran_output *output) {
    // NOTE: Change this to an early-return if we make this a public function.
    assert(output->capture.video_stage == SCRAN_VIDEO_STAGE_FULLSCREEN_START_PENDING);
    output->capture.video_stage = SCRAN_VIDEO_STAGE_NONE;
    selection_unfreeze_size(output);
}

// Should only be called once the video capture event loop is finished.
//    Call video_capture_request_stop() instead to initiate graceful completion.
void
capture_video_finish(struct scran_output *st_output)
{
    struct scran_output_capture *capture    = &st_output->capture;
    struct ffmpeg_context       *ffmpeg_ctx = &capture->ffmpeg_ctx;

    if (capture->audio_active) {
        scran_pipewire_reset();
        capture_video_drain_writer(
            st_output,
            ffmpeg_ctx->av_codec_ctx_audio,
            ffmpeg_ctx->av_packet_audio,
            capture_video_write_audio_packet,
            "audio"
        );
        capture_video_destroy_audio_writer(st_output);
        capture->audio_active = false;
    }

    capture_video_drain_writer(
        st_output,
        ffmpeg_ctx->av_codec_ctx,
        ffmpeg_ctx->av_packet,
        capture_video_write_video_packet,
        "video"
    );

    {
        // NOTE: Do not use g_state.options.output_path, since it is shared by
        // image-capture.
        const char *output_path = g_state.options.output_to_stdout ? NULL : ffmpeg_ctx->av_format_ctx->url;

        av_write_trailer(ffmpeg_ctx->av_format_ctx);
        clipboard_update(&g_state.seat.datacontrol, NULL, NULL, output_path);

        if (output_path) {
            eprintf("Video saved: %s\n", output_path);
            scran_portal_notify_file_saved(output_path);
        }
    }
    capture_video_destroy_video_writer(st_output);

    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_color(   SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, SCRAN_UI_COLOR_DEFAULT);
        scran_ui_textline_item_set_locked(  SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_VIDEO, false);
        scran_ui_statusline_set_timer(&st_output->selection_surface.ui_ctx.ui_statusline, 0);
    }
    selection_surface_set_theme(st_output, st_output->capture.pre_capture_selection_theme);
    cursor_set_theme(st_output, SCRAN_CURSOR_THEME_DEFAULT);
    request_selection_surface_frame_callback(st_output);

    scran_stdout_release(&st_output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO);

    selection_unfreeze_size(st_output);

    if (capture->fullscreen_consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        capture_fullscreen_end(st_output, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO);
    }

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    st_output->capture.video_stage = SCRAN_VIDEO_STAGE_NONE;

    DEBUG("FINISHED RECORDING.\n");
}

void
capture_video_request_stop(struct scran_output *st_output)
{
    struct scran_output_capture *capture = &st_output->capture;
    struct capture_frame_context *frame_ctx = &capture->session.frame_ctx;
    const BLPointI source_dimensions_px = st_output->capture.session.session_ctx.source_dimensions_px;

    // TODO: Just assert instead?
    if (capture->video_stage == SCRAN_VIDEO_STAGE_STOP_REQUESTED) {
        return;
    }
    capture->video_stage = SCRAN_VIDEO_STAGE_STOP_REQUESTED;

    ext_image_copy_capture_frame_v1_destroy(frame_ctx->frame);
    frame_ctx->frame = NULL;

    // Ensure one last frame is triggered as soon as possible, even if
    // no damage has been reported by the compositor. This ensures
    // variable framerate recordings will end at an appropriate
    // timestamp. This also lets the frame listener finalize the
    // recording and clean up as soon as possible.

    capture_request_frame_forced(
        &st_output->capture.session, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO,
        // XXX: This damage request is probably normally redundant with
        // capture_request_frame_forced(), but should stay regardless, in case
        // the initial frame was interrupted before it came back (i.e. making
        // it a 1-frame video, once this frame is processed), since the first
        // frame in a session should always have full damage.
        &(BLRectI){ 0, 0, source_dimensions_px.x, source_dimensions_px.y }
    );
}


static void
print_slurp_string(BLRectI rect)
{
    // TODO: Assert nothing else was sent to stdout?
    fprintf(stdout, "%d,%d %dx%d\n", rect.x, rect.y, rect.w, rect.h);
    fflush(stdout);
}

static void
print_slurp_string_selection(struct scran_output *st_output)
{
    const double scale = st_output->selection_surface.surface.final_scale_factor_normalized;
    const struct scran_output_xdg_geometry geometry = st_output->xdg_geometry;
    const struct BLBoxI box_px = selection_get_box_px(&st_output->selection_ctx);

    const struct BLRectI rect_logical = {
        .x = round(  box_px.x0              / scale),
        .y = round(  box_px.y0              / scale),
        .w = round( (box_px.x1 - box_px.x0) / scale),
        .h = round( (box_px.y1 - box_px.y0) / scale),
    };

    const struct BLRectI rect_logical_global = {
        .x = geometry.x_logical + rect_logical.x,
        .y = geometry.y_logical + rect_logical.y,
        .w = rect_logical.w,
        .h = rect_logical.h
    };

    print_slurp_string(rect_logical_global);
}

static void
print_slurp_string_fullscreen(struct scran_output *st_output)
{
    print_slurp_string(
        (BLRectI){
            .x = st_output->xdg_geometry.x_logical,
            .y = st_output->xdg_geometry.y_logical,
            .w = st_output->xdg_geometry.w_logical,
            .h = st_output->xdg_geometry.h_logical,
        }
    );
}

bool
capture_image_start(struct scran_output *st_output, bool exit_after_capture)
{
    struct capture_session *session              = &st_output->capture.session;
    const BLPointI          source_dimensions_px = session->session_ctx.source_dimensions_px;

    bool success = false;

    if (g_state.options.produce_slurp) {
        if (scran_stdout_is_reserved()) {
            scran_stdout_print_busy_message();
            exit_after_capture = false;
        } else {
            print_slurp_string_selection(st_output);
            success = true;
        }
    } else if (session->frame_ctx.consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        eprintf("Image capture already in progress...\n");
    } else if (g_state.options.output_to_stdout
               && !scran_stdout_try_reserve(&st_output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE)
    ) {
        scran_stdout_print_busy_message();
        // Only allow upgrading pending *images* to exit_after_capture.
        // Our consumers check above should have ensured the assert holds.
        assert(!scran_stdout_check_reservation(&st_output->capture.stdout_reservation,SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE));
        exit_after_capture = false;
    } else {
        capture_request_frame_forced(
            session, SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE,
            &(BLRectI){ 0, 0, source_dimensions_px.x, source_dimensions_px.y }
        );
        atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
        success = true;
    }

    if (exit_after_capture) {
        // XXX TODO: Put this in a generic end_capture() function.
        scran_request_exit();
    }

    return success;
}

void
capture_image_finish(struct scran_output *output)
{
    if (output->capture.fullscreen_consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        capture_fullscreen_end(output, SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE);
    }

    if (g_state.options.output_to_stdout) {
        assert(scran_stdout_check_reservation(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE));
        scran_stdout_release(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE);
    }

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


bool
capture_image_start_fullscreen(struct scran_output *st_output, bool exit_after_capture)
{

    if (g_state.options.produce_slurp) {
        if (scran_stdout_is_reserved()) {
            scran_stdout_print_busy_message();
            return false;
        } else {
            print_slurp_string_fullscreen(st_output);
            if (exit_after_capture) {
                // XXX TODO: Put this in a generic end_capture() function.
                scran_request_exit();
            }
        }
        return true;
    }

    bool prev_exit_after_capture = st_output->capture.exit_after_capture;

    // Must be set prior to capture_fullscreen_start(), since it will dispatch
    // the capture instantly when possible.
    st_output->capture.exit_after_capture = exit_after_capture;

    if (capture_fullscreen_start(
            st_output,
            SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE)
    ) {
        return true;
    } else {
        st_output->capture.exit_after_capture = prev_exit_after_capture;
    }

    return false;
}
