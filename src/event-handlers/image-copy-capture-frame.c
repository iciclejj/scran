#include <assert.h>
#include <stdatomic.h>

#include <ext-image-copy-capture-v1.h>

#include "capture.h"
#include "event-handlers.h"
#include "print.h"
#include "selection.h"
#include "scranrot.h"
#include "state.h"
#include "state-util.h"
#include "ui.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"


static inline void
capture_create_buffer_area_context(
    const struct scran_output *output,
    const struct capture_session_context *session,
    const struct capture_frame_context *frame_ctx,
    struct capture_buffer_area_context *buffer_area_ctx
) {
    buffer_area_ctx->area_px = capture_get_selection_as_capture_buffer_area_px(
        &output->capture,
        session,
        frame_ctx
    );
    buffer_area_ctx->area_start_address = capture_get_area_start_address(
        session,
        frame_ctx,
        &buffer_area_ctx->area_px
    );
    buffer_area_ctx->source_row_bytes =
        session->pixel_stride * session->source_dimensions_px.x;
}


static void
handle_image_copy_capture_frame_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;
    frame_ctx->source_transform = transform;
}


static void
handle_image_copy_capture_frame_damage(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    struct capture_frame_context *frame_ctx = data;
    capture_grow_tracked_damage(frame_ctx, x, y, width, height);
}


static void
handle_image_copy_capture_frame_presentation_time(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    struct capture_frame_context *frame_ctx = data;

    // XXX: Will overflow at tv_sec > ~584.9 years...
    const int64_t tv_sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = tv_sec_to_nsec + tv_nsec;
}


static inline void
continue_after_showing_freezeframe(
    struct scran_output *output
) {
    struct scran_output_freezeframe *freezeframe = &output->freezeframe;

    if (freezeframe->unhide_after_capture) {
        release_selection_surface_hide(output, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FREEZEFRAME_HIDE);
        freezeframe->unhide_after_capture = false;
    }

    scran_output_callback callback = freezeframe->callback;
    assert(callback != NULL);
    freezeframe->callback = NULL;
    callback(output);
}


static void display_freezeframe(struct scran_output *output);

static void
display_freezeframe_after_buffer_release(struct scran_wl_buffer *buffer)
{
    struct scran_output *output = &g_state.outputs[get_containing_output_array_index(buffer)];
    display_freezeframe(output);
}

static void
display_freezeframe(struct scran_output *output)
{
    struct scran_output_freezeframe *freezeframe = &output->freezeframe;

    struct capture_frame_context    *frame_ctx      = &freezeframe->session.frame_ctx;
    struct scran_wl_buffer          *capture_buffer = &frame_ctx->scran_wl_buffer;
    struct scran_wl_buffer          *surface_buffer = &freezeframe->surface_buffer;
    const struct capture_session_context *session = &freezeframe->session.session_ctx;

    assert(capture_buffer->busy == false); // We should not have started capture if busy

    struct scran_wl_buffer *final_buffer;

    enum wl_output_transform       buffer_transform = -1;
    const int32_t                  source_width_px  = session->source_dimensions_px.x;
    const int32_t                  source_height_px = session->source_dimensions_px.y;
    const enum wl_output_transform source_transform = freezeframe->session.frame_ctx.source_transform;

    // XXX TODO: Rework this once scranrot supports flipped
    // XXX TODO: Refactor this to make it more readable...

    // Show the new, just-captured freezeframe
    if (source_transform == WL_OUTPUT_TRANSFORM_NORMAL || source_transform == WL_OUTPUT_TRANSFORM_FLIPPED) {
        final_buffer     = capture_buffer;
        buffer_transform = source_transform;
    } else {
        bool source_is_flipped = source_transform >= WL_OUTPUT_TRANSFORM_FLIPPED;
        enum wl_output_transform scranrot_transform = source_is_flipped ? source_transform - WL_OUTPUT_TRANSFORM_FLIPPED : source_transform;

        if (surface_buffer->busy) {
            surface_buffer->release_callback = display_freezeframe_after_buffer_release;
            return;
        }

        assert(get_transformed_width(source_width_px, source_height_px, source_transform) == freezeframe->subsurface.width_px_buffer);
        assert(get_transformed_height(source_width_px, source_height_px, source_transform) == freezeframe->subsurface.height_px_buffer);

        size_t dst_stride = 0;
        // See comments referencing #14441 for why we scranrot instead of just ::set_buffer_transform().
        if (scranrot_transform_framebuffer(
                capture_buffer->data, source_width_px, source_height_px, source_width_px * session->pixel_stride,
                surface_buffer->data,
                RGBA32_SHUFFLE_NO_CHANGE, (enum scranrot_transform)scranrot_transform,
                &dst_stride)
        ) {
            assert(dst_stride < INT_MAX && (int)dst_stride == freezeframe->subsurface.width_px_buffer * session->pixel_stride);
            final_buffer     = surface_buffer;
            buffer_transform = source_is_flipped ? WL_OUTPUT_TRANSFORM_FLIPPED : WL_OUTPUT_TRANSFORM_NORMAL;
        } else {
            eprintf("WARNING: Scranrot failed to convert freezeframe buffer; falling back to set_buffer_transform.\n");
            // XXX TODO: This does not work correctly yet without an actual surface.transform
            // property to check against in the update_scale_size_viewport() functions.
            final_buffer     = capture_buffer;
            buffer_transform = source_transform;
        }
    }
    const int final_width_px  = final_buffer == capture_buffer ? source_width_px  : freezeframe->subsurface.width_px_buffer;
    const int final_height_px = final_buffer == capture_buffer ? source_height_px : freezeframe->subsurface.height_px_buffer;

    final_buffer->busy = true;
    wl_surface_attach(freezeframe->subsurface.wl_surface, final_buffer->wl_buffer, 0, 0);
    wl_surface_set_buffer_transform(freezeframe->subsurface.wl_surface, buffer_transform);
    wl_surface_damage_buffer(freezeframe->subsurface.wl_surface, 0, 0, final_width_px, final_height_px);
    wl_surface_commit(freezeframe->subsurface.wl_surface);
    freezeframe->showing = true;
    {
        struct scran_ui_context *ui_ctx = &output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_text( SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_OFF);
        scran_ui_textline_item_set_color(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME);
    }

    continue_after_showing_freezeframe(output);
}


static void
handle_image_copy_capture_frame_ready(
    void *data,
    struct ext_image_copy_capture_frame_v1 *wl_frame
) {
    ext_image_copy_capture_frame_v1_destroy(wl_frame);

    struct capture_frame_context *frame_ctx = data;
    struct scran_output          *output    = frame_ctx->output;
    frame_ctx->frame = NULL;

    const bool image_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
    const bool video_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
    const bool freezeframe_requested = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME;

    assert(image_requested || video_requested || freezeframe_requested);

    if (freezeframe_requested) {
        display_freezeframe(output);
    }

    if (image_requested || video_requested) {
        const struct capture_session_context *session = &output->capture.session.session_ctx;
        struct capture_buffer_area_context buffer_area_ctx;
        capture_create_buffer_area_context(output, session, frame_ctx, &buffer_area_ctx);

        if (image_requested) {
            // XXX: Capturing image during video capture not implemented yet...
            assert(!output->capture.capturing_video);
            assert(g_state.n_captures_in_progress >= 1);

            capture_image_write_image(output, session, frame_ctx, &buffer_area_ctx);
            capture_image_finish(output);
        }

        if (video_requested) {
            if (blboxi_intersects(buffer_area_ctx.area_px, frame_ctx->capture_buffer_damage_area_px)) {
                if (!capture_video_write_video_frame(output, frame_ctx, session, &buffer_area_ctx)) {
                    output->capture.video_end_requested = true;
                }
            }

            // NOTE: We do this check *after* writing the incoming frame. This ensures
            // that the video will not be cut short at the end if we're only capturing
            // frames on demand (with variable framerate) and nothing has changed for
            // the last x amount of time.
            // Forcing some compositor/surface damage when signaling to end the capture
            // should trigger the necessary final frame.
            //
            // TODO: Go through uses of capturing_video to check for redundancy now
            // that we have a global state, with e.g. `.exit_requested`.
            if (output->capture.video_end_requested || g_state.exit_requested) {
                capture_video_finish(output);
            } else {
                // TODO: avio_flush ?
                capture_request_frame(&output->capture.session, SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO, NULL);
            }
        }
    }

    frame_ctx->capture_buffer_damage_area_px = (BLBoxI){0};
}


static void
handle_image_copy_capture_frame_failed(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct capture_frame_context *frame_ctx = data;
    struct scran_output          *output    = frame_ctx->output;
    frame_ctx->frame = NULL;

    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        eprintf("ERROR: freezeframe capture failed (%u)\n", reason);
        // FIXME: Handle this better?
        continue_after_showing_freezeframe(output);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        capture_image_finish(output);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        // TODO: Retry a few times?
        capture_video_finish(output);
    }
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = handle_image_copy_capture_frame_presentation_time,
    .ready = handle_image_copy_capture_frame_ready,
    .failed = handle_image_copy_capture_frame_failed,
};
