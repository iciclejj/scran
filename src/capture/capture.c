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
capture_update_selection(struct scran_output *output, BLBoxI selection_ctx_box_px) {
    struct scran_output_capture *capture = &output->capture;

    bool size_changed =
           blboxi_width_abs_unsafe(capture->selection_ctx_box_px)  != blboxi_width_abs_unsafe(selection_ctx_box_px)
        || blboxi_height_abs_unsafe(capture->selection_ctx_box_px) != blboxi_height_abs_unsafe(selection_ctx_box_px);

    // Presentation feedback for an older selection-surface buffer can arrive
    // after video capture has frozen the selection size.
    if (output->selection_ctx.size_is_frozen && size_changed) {
        return;
    }

    capture->selection_ctx_box_px = selection_ctx_box_px;
}


bool
capture_request_frame(
    struct capture_view view,
    enum scran_capture_frame_consumer_mask consumer,
    const BLRectI *damage
) {
    struct capture_frame_context *frame_ctx = view.frame_ctx;

    if (frame_ctx->frame) {
        frame_ctx->consumers |= consumer;
        if (damage != NULL) {
            // Forward the damage to the in-flight frame, since frame::damage_buffer can only be
            // requested prior to frame::capture().
            //   WARNING: This is only safe because we never actually put the framebuffer
            //   into an incoherent state in-between frame requests. Otherwise, we would need
            //   to start destroying and re-requesting new frames to properly handle that damage.
            capture_grow_tracked_damage(frame_ctx, damage->x, damage->y, damage->w, damage->h);
        }
        return true;
    }

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(view.session_ctx->wl_session);

    ext_image_copy_capture_frame_v1_attach_buffer(frame, frame_ctx->scran_wl_buffer.wl_buffer);
    ext_image_copy_capture_frame_v1_add_listener(frame, &image_copy_capture_frame_listener, frame_ctx);

    // TODO: inline to remove branch
    if (damage != NULL) {
        capture_damage_buffer(frame_ctx, frame, damage->x, damage->y, damage->w, damage->h);
    }

    frame_ctx->frame = frame;
    frame_ctx->consumers |= consumer;

    ext_image_copy_capture_frame_v1_capture(frame);

    return true;
}

static inline void capture_video_cancel_pending_fullscreen_capture(struct scran_output *output);

enum scran_capture_frame_consumer_mask
capture_fullscreen_dispatch_awaiting_consumers(
    struct scran_output *output,
    enum scran_capture_frame_consumer_mask awaiting
) {
    struct scran_output_capture *capture = &output->capture;
    enum scran_capture_frame_consumer_mask started = 0;

    capture->fullscreen_consumers.active |= awaiting;

    if (awaiting & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        if (capture_image_start(output, capture->exit_after_capture)) {
            started |= SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
        }
    }

    if (awaiting & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        capture->audio_disable_modifier_active = capture->fullscreen_video_pending_audio_disabled;
        capture->fullscreen_video_pending_audio_disabled = false;

        if (!g_state.exit_requested && capture_video_start(output)) {
            started |= SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
        } else {
            capture_video_cancel_pending_fullscreen_capture(output);
        }
    }

    if (awaiting & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        // TODO: Make freezeframe able to not set started?
        started |= SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME;
        freezeframe_capture_start_retain_callback(output);
    }

    enum scran_capture_frame_consumer_mask failed = awaiting & ~started;
    if (failed) {
        capture_fullscreen_end(output, failed);
    }

    return started;
}

static void
capture_fullscreen_acquire_ui_hide(struct scran_output *output)
{
    enum scran_fullscreen_ui_state *state = &output->capture.fullscreen_ui_state;

    if (*state == SCRAN_FULLSCREEN_UI_HIDDEN || *state == SCRAN_FULLSCREEN_UI_HIDE_PENDING) {
        return;
    }

    DEBUG("Acquiring fullscreen hide\n");
    *state = SCRAN_FULLSCREEN_UI_HIDE_PENDING;
    selection_surface_acquire_hide_then(
        output,
        &presentation_feedback_listener__transparent_selection_capture,
        SCRAN_SELECTION_SURFACE_DISABLE_REASON_FULLSCREEN_HIDE
    );
}

static void
capture_fullscreen_release_ui_hide(struct scran_output *output)
{
    enum scran_fullscreen_ui_state *state = &output->capture.fullscreen_ui_state;
    struct scran_fullscreen_consumers *consumers = &output->capture.fullscreen_consumers;

    assert(*state == SCRAN_FULLSCREEN_UI_HIDDEN);

    // Set desired state first, since the release hide will redraw the selection
    // surface, which selects fullscreen capture UI according to this.
    *state = consumers->active | consumers->awaiting_ui ? SCRAN_FULLSCREEN_UI_SHOW_PENDING : SCRAN_FULLSCREEN_UI_NONE;
    selection_surface_release_hide(output, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FULLSCREEN_HIDE);
    DEBUG("Released fullscreen hide\n");
}

void
capture_fullscreen_sync_ui_and_dispatch(struct scran_output *output)
{
    struct scran_fullscreen_consumers *consumers = &output->capture.fullscreen_consumers;
    enum scran_capture_frame_consumer_mask all_consumers = consumers->active | consumers->awaiting_ui;
    bool ui_should_be_hidden =
        all_consumers
        && (!output->selection_surface.ui_inside_selection
            || !capture_fullscreen_consumers_allow_ui(all_consumers));

    switch (output->capture.fullscreen_ui_state) {
    case SCRAN_FULLSCREEN_UI_SHOW_PENDING:
    case SCRAN_FULLSCREEN_UI_HIDE_PENDING:
        // Don't interfere with in-progress transitions.
        // The ::presented handlers should call this function when they're done.
        return;
    case SCRAN_FULLSCREEN_UI_NONE:
        if (all_consumers) {
            if (ui_should_be_hidden) {
                capture_fullscreen_acquire_ui_hide(output);
            } else {
                // Set state first, since the drawing function checks it.
                output->capture.fullscreen_ui_state = SCRAN_FULLSCREEN_UI_SHOW_PENDING;
                draw_selection_and_commit(output);
            }
        }
        return;
    case SCRAN_FULLSCREEN_UI_SHOWN:
        if (!all_consumers) {
            output->capture.fullscreen_ui_state = SCRAN_FULLSCREEN_UI_NONE;
            draw_selection_and_commit(output);
            return;
        }
        if (ui_should_be_hidden) {
            capture_fullscreen_acquire_ui_hide(output);
            return;
        }
        break;
    case SCRAN_FULLSCREEN_UI_HIDDEN:
        if (!ui_should_be_hidden) {
            capture_fullscreen_release_ui_hide(output);
            return;
        }
        break;
    }

    enum scran_capture_frame_consumer_mask were_awaiting_ui = consumers->awaiting_ui;
    consumers->awaiting_ui = 0;
    if (were_awaiting_ui) {
        capture_fullscreen_dispatch_awaiting_consumers(output, were_awaiting_ui);
    }
}

// TODO:
//
//   Unify the fullscreen capture pipeline and the regular capture pipeline.
//
//      At time of writing, inside-mode UI is captured during non-fullscreen
//      image capture.
//
//      This and potentially other similar behavior would be much easier to
//      maintain and handle properly if we just merge everything into one
//      capture_start()/capture_end() pipeline, which will route both
//      fullscreen captures and regular captures through an equivalent
//      UI-syncing mechanism to what fullscreen already uses, and so on.
//

enum scran_capture_frame_consumer_mask
capture_fullscreen_start(
    struct scran_output *output,
    enum scran_capture_frame_consumer_mask incoming_consumers
) {
    struct scran_fullscreen_consumers *consumers = &output->capture.fullscreen_consumers;
    enum scran_capture_frame_consumer_mask new = incoming_consumers & ~(consumers->awaiting_ui | consumers->active);

    // TODO: Put the exit_requested checks at better boundaries, e.g. one shared
    // capture_start function.
    if (!new || g_state.exit_requested) {
        return 0;
    }

    if (!consumers->awaiting_ui && !consumers->active) {
        // HACK: Prevent `capture-button -> exit-button` being able to exit
        // prematurely in case of still-awaiting consumers->awaiting_ui.
        // This adds a "fake" capture to the counter.
        atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
    }

    consumers->awaiting_ui |= new;
    capture_fullscreen_sync_ui_and_dispatch(output);

    // The dispatch has removed consumers that failed to start
    return new & (consumers->active | consumers->awaiting_ui);
}

void
capture_fullscreen_end(
    struct scran_output *output,
    enum scran_capture_frame_consumer_mask finished_consumers
) {
    struct scran_fullscreen_consumers *consumers = &output->capture.fullscreen_consumers;

    consumers->active &= ~finished_consumers;

    if (!consumers->active && !consumers->awaiting_ui) {
        // HACK: See comment in capture_fullscreen_start().
        atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
    }

    capture_fullscreen_sync_ui_and_dispatch(output);
}


bool
capture_video_start(struct scran_output *output)
{
    const struct capture_view view = capture_view_from_frame(&output->capture.frame_ctx);

    // TODO: Assert instead?
    if (capture_video_is_live(output)) {
        DEBUG("Already capturing...\n");
        return false;
    }

    selection_freeze_size(output);

    const bool fullscreen = output->capture.fullscreen_consumers.active & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
    const BLPointI dimensions = fullscreen
        ? blboxi_get_dimensions(get_fullscreen_selection_box(output))
        : blboxi_get_dimensions(output->capture.selection_ctx_box_px);

    // TODO: Assert box is within output dimensions
    assert(dimensions.x && dimensions.y);

    if (g_state.options.output_to_stdout) {
        if (!scran_stdout_try_reserve(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO)) {
            scran_stdout_print_busy_message();
            goto capture_video_start_fail_1;
        }
    }

    if (!capture_video_init_writers(output, dimensions)) {
        eprintf("Error: Failed to initialize ffmpeg libraries.\n");
        // TODO: goto fail if this becomes more complicated
        goto capture_video_start_fail_2;
    }

    // TODO: Cache surface border color and add it to main.c::update_ui()?
    output->capture.pre_capture_border_color = output->selection_surface.border_color;
    selection_surface_set_border_color(output, UI_COLOR_VIDEO_CAPTURE);
    request_selection_surface_frame_callback(output);

    output->capture.video_presentation_time_nsec_start = capture_clock_gettime_nsec();

    // Get initial frame. Subsequent capture requests happen within
    // frame::ready, similar to the wl_surface callback event loop
    capture_request_frame_forced(view, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO);


    if (output->capture.audio_active) {
        scran_pipewire_connect();
    }

    output->capture.video_stage = SCRAN_VIDEO_STAGE_CAPTURING;
    atomic_fetch_add_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    return true;

capture_video_start_fail_2:
    scran_stdout_release(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO);
capture_video_start_fail_1:
    selection_unfreeze_size(output);
    return false;
}

bool
capture_video_start_fullscreen(struct scran_output *output)
{
    struct scran_output_capture *capture = &output->capture;

    // TODO: Reserve stdout already here, once we have better capture-state
    // tracking with e.g. an enum

    // TODO: Assert instead?
    if (capture_video_is_live(output)) {
        DEBUG("Already capturing...\n");
        return false;
    }

    bool prev_pending_audio_disabled = capture->fullscreen_video_pending_audio_disabled;

    // Must be set prior to capture_fullscreen_start(), since it will dispatch
    // the capture instantly when possible.
    capture->fullscreen_video_pending_audio_disabled = capture->audio_disable_modifier_active;
    capture->video_stage                             = SCRAN_VIDEO_STAGE_FULLSCREEN_START_PENDING;

    if (!capture_fullscreen_start(
            output,
            SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO)
    ) {
        capture->fullscreen_video_pending_audio_disabled = prev_pending_audio_disabled;
        capture->video_stage                             = SCRAN_VIDEO_STAGE_NONE;
        return false;
    }

    // Freeze already here to block entering SELECTION_INITIALIZING
    selection_freeze_size(output);

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
capture_video_finish(struct scran_output *output)
{
    struct scran_output_capture *capture    = &output->capture;
    struct ffmpeg_context       *ffmpeg_ctx = &capture->ffmpeg_ctx;

    if (capture->audio_active) {
        scran_pipewire_reset();
        capture_video_drain_writer(
            output,
            ffmpeg_ctx->av_codec_ctx_audio,
            ffmpeg_ctx->av_packet_audio,
            capture_video_write_audio_packet,
            "audio"
        );
        capture_video_destroy_audio_writer(output);
        capture->audio_active = false;
    }

    capture_video_drain_writer(
        output,
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
    capture_video_destroy_video_writer(output);

    selection_surface_set_border_color(output, output->capture.pre_capture_border_color);
    request_selection_surface_frame_callback(output);

    scran_stdout_release(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO);

    selection_unfreeze_size(output);

    if (capture->fullscreen_consumers.active & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        capture_fullscreen_end(output, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO);
    }

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    output->capture.video_stage = SCRAN_VIDEO_STAGE_NONE;

    DEBUG("FINISHED RECORDING.\n");
}

void
capture_video_request_stop(struct scran_output *output)
{
    struct scran_output_capture *capture = &output->capture;
    const struct capture_view    view    = capture_view_from_frame(&capture->frame_ctx);

    // TODO: Just assert instead?
    if (capture->video_stage == SCRAN_VIDEO_STAGE_STOP_REQUESTED) {
        return;
    }
    capture->video_stage = SCRAN_VIDEO_STAGE_STOP_REQUESTED;

    capture_destroy_frame(view);

    // Ensure one last frame is triggered as soon as possible, even if
    // no damage has been reported by the compositor. This ensures
    // variable framerate recordings will end at an appropriate
    // timestamp. This also lets the frame listener finalize the
    // recording and clean up as soon as possible.

    capture_request_frame_forced(view, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO);
}


static void
print_slurp_string(BLRectI rect)
{
    // TODO: Assert nothing else was sent to stdout?
    fprintf(stdout, "%d,%d %dx%d\n", rect.x, rect.y, rect.w, rect.h);
    fflush(stdout);
}

static void
print_slurp_string_selection(struct scran_output *output)
{
    const double scale = output->selection_surface.surface.final_scale_factor_normalized;
    const struct scran_output_xdg_geometry geometry = output->xdg_geometry;
    const struct BLBoxI box_px = selection_get_box_px(&output->selection_ctx);

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
print_slurp_string_fullscreen(struct scran_output *output)
{
    print_slurp_string(
        (BLRectI){
            .x = output->xdg_geometry.x_logical,
            .y = output->xdg_geometry.y_logical,
            .w = output->xdg_geometry.w_logical,
            .h = output->xdg_geometry.h_logical,
        }
    );
}

bool
capture_image_start(struct scran_output *output, bool exit_after_capture)
{
    const struct capture_view view = capture_view_from_frame(&output->capture.frame_ctx);

    bool success = false;

    if (g_state.options.produce_slurp) {
        if (scran_stdout_is_reserved()) {
            scran_stdout_print_busy_message();
            exit_after_capture = false;
        } else {
            print_slurp_string_selection(output);
            success = true;
        }
    } else if (view.frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        eprintf("Image capture already in progress...\n");
    } else if (g_state.options.output_to_stdout
               && !scran_stdout_try_reserve(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE)
    ) {
        scran_stdout_print_busy_message();
        // Only allow upgrading pending *images* to exit_after_capture.
        // Our consumers check above should have ensured the assert holds.
        assert(!scran_stdout_check_reservation(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE));
        exit_after_capture = false;
    } else {
        capture_request_frame_forced(view, SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE);
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
    if (output->capture.fullscreen_consumers.active & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        capture_fullscreen_end(output, SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE);
    }

    if (g_state.options.output_to_stdout) {
        assert(scran_stdout_check_reservation(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE));
        scran_stdout_release(&output->capture.stdout_reservation, SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE);
    }

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


bool
capture_image_start_fullscreen(struct scran_output *output, bool exit_after_capture)
{

    if (g_state.options.produce_slurp) {
        if (scran_stdout_is_reserved()) {
            scran_stdout_print_busy_message();
            return false;
        } else {
            print_slurp_string_fullscreen(output);
            if (exit_after_capture) {
                // XXX TODO: Put this in a generic end_capture() function.
                scran_request_exit();
            }
        }
        return true;
    }

    bool prev_exit_after_capture = output->capture.exit_after_capture;

    // Must be set prior to capture_fullscreen_start(), since it will dispatch
    // the capture instantly when possible.
    output->capture.exit_after_capture = exit_after_capture;

    if (capture_fullscreen_start(
            output,
            SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE)
    ) {
        return true;
    } else {
        output->capture.exit_after_capture = prev_exit_after_capture;
    }

    return false;
}
