#include <assert.h>
#include <limits.h>

#include <wayland-client-protocol.h>

#include "selection.h"
#include "ui.h"
#include "viewporter.h"

#include "state.h"
#include "state-util.h"
#include "capture.h"
#include "freezeframe.h"
#include "selection-surface.h"
#include "print.h"
#include "scranrot.h"
#include "util/lib-interop.h"


static void
freezeframe_capture_start_after_buffer_release(struct scran_wl_buffer *buffer)
{
    struct scran_output *output = &g_state.outputs[get_containing_output_array_index(buffer)];
    freezeframe_capture_start_assume_callback_set(output);
}


void
freezeframe_capture_start_assume_callback_set(struct scran_output *st_output)
{
    assert(st_output->freezeframe.callback != NULL);

    struct capture_session *session              = &st_output->freezeframe.session;
    const  BLPointI         source_dimensions_px = session->session_ctx.source_dimensions_px;

    if (session->frame_ctx.scran_wl_buffer.busy) { // XXX: Not thread-safe.
        session->frame_ctx.scran_wl_buffer.release_callback = freezeframe_capture_start_after_buffer_release;
        return;
    }

    capture_request_frame_forced(
        session, SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME,
        &(BLRectI){ 0, 0, source_dimensions_px.x, source_dimensions_px.y }
    );
}

// Use freezeframe_capture_refresh post-init/during normal runtime
void
freezeframe_capture_start(
    struct scran_output *st_output,
    scran_output_callback callback
) {
    assert(callback != NULL);
    assert(st_output->freezeframe.callback == NULL);

    st_output->freezeframe.callback = callback;
    freezeframe_capture_start_assume_callback_set(st_output);
}

void
freezeframe_hide_if_showing(struct scran_output *st_output)
{
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    if (!freezeframe->showing) {
        return;
    }

    // NOTE(!!): We need to actually unmap this surface, and not just
    // attach a transparent buffer, since attaching a transparent
    // buffer causes some compositors (e.g. Sway) to not properly
    // damage/redraw what was underneath, resulting in a black screen
    // until something actually needs damage. I assume this is a bug.
    wl_surface_attach(freezeframe->subsurface.wl_surface, NULL, 0, 0);
    // FIXME: Is this still needed? Remove this if possible, after testing on all compositors.
    wl_surface_damage_buffer(
        freezeframe->subsurface.wl_surface,
        0, 0, freezeframe->subsurface.width_px_buffer, freezeframe->subsurface.height_px_buffer
    );
    wl_surface_commit(freezeframe->subsurface.wl_surface);

    // HACK: If we're capturing fullscreen video (where we attach a transparent
    // buffer to our selection-surface), some compositors (Hyprland) will not
    // properly update the screen to remove our freezeframe, in areas where it
    // doesn't detect any change.
    selection_do_some_damage(st_output);

    // XXX: These should theoretically be set after the commit goes through
    freezeframe->showing = false;
    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_text( SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_ON);
        scran_ui_textline_item_set_color(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_COLOR_DEFAULT);
        request_selection_surface_frame_callback(st_output);
    }
}

static void
freezeframe_capture_finish(
    struct scran_output *output
) {
    struct scran_output_freezeframe *freezeframe = &output->freezeframe;

    // We can also come here during startup with -z, in which case we can bypass
    // the regular fullscreen capture pipeline
    if (output->capture.fullscreen_consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        capture_fullscreen_end(output, SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME);
    }

    scran_output_callback callback = freezeframe->callback;
    assert(callback != NULL);
    freezeframe->callback = NULL;
    callback(output);
}


void freezeframe_capture_handle_frame_ready(struct scran_output *output);

static void
freezeframe_show_after_buffer_release(struct scran_wl_buffer *buffer)
{
    struct scran_output *output = &g_state.outputs[get_containing_output_array_index(buffer)];
    freezeframe_capture_handle_frame_ready(output);
}

// Tries to display the freezeframe.
// If surface-buffer is busy, it will abort and re-run after buffer release.
void
freezeframe_capture_handle_frame_ready(struct scran_output *output)
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
            surface_buffer->release_callback = freezeframe_show_after_buffer_release;
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

    freezeframe_capture_finish(output);
}


void
freezeframe_capture_handle_failed(
    struct scran_output *output,
    uint32_t reason
) {
    eprintf("ERROR: freezeframe capture failed (%u)\n", reason);
    // FIXME: Handle this better?
    freezeframe_capture_finish(output);
}


void
freezeframe_capture_refresh(
    struct scran_output *st_output,
    scran_output_callback callback
) {
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    if (freezeframe->callback != NULL) {
        eprintf("Freezeframe already in progress.\n");
        return;
    }
    assert(callback != NULL);
    freezeframe->callback = callback;

    freezeframe_hide_if_showing(st_output);

    capture_fullscreen_start(st_output, SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME);
}

void
freezeframe_surface_update_scale_size_viewport(
    struct scran_output *st_output
) {
    struct scran_output_freezeframe *freezeframe    = &st_output->freezeframe;
    struct scran_output_surface     *parent_surface = &st_output->selection_surface.surface;

    DEBUG("  freezeframe_surface_update_scale_size_viewport()\n");

    // We "hardcode" these for freezeframe, since it should equal to the capture
    // buffer size.
    //   NOTE: These cannot simply be set during init_premem, since output::mode()
    int32_t width_px_buffer  = get_transformed_output_width(st_output);
    int32_t height_px_buffer = get_transformed_output_height(st_output);

    if ( !(width_px_buffer && height_px_buffer)) {
        DEBUG("    Invalid buffer dimensions; skipping. output::mode() probably did not fire yet.\n");
        return;
    }

    double scale = get_surface_scale_factor_normalized(parent_surface);

    if (parent_surface->width_logical && parent_surface->height_logical) {
        // We neeed to base the source dimensions on the logical
        // dimensions for fractional scaling to stay sharp.
        //
        // TODO: Make shared helper function for all our logical -> buffer
        // scaling logic
        int32_t width_px_buffer_scalesafe  = round(parent_surface->width_logical  * scale);
        int32_t height_px_buffer_scalesafe = round(parent_surface->height_logical * scale);

        // Clamp them to output width, in case the compositor is trying to
        // downscale rather than pixel-perfect scaling
        //
        // Some compositors allow +1 (and/or expect their scaling to reach
        // within +1 of the actually expected buffer size), but not all. Either
        // way, results seem to be the same if clamping fully down, so we just
        // clamp all the way down in all cases.
        //
        // NOTE: Remember to apply transform before clamping, if using
        // set_buffer_transform, since the viewport will expect transformed
        // width/height (and logical coordinates from above will be in that
        // orientation as well). See also comments referencing #14441.
        if (width_px_buffer_scalesafe  > width_px_buffer) {
            width_px_buffer_scalesafe  = width_px_buffer;
        }
        if (height_px_buffer_scalesafe > height_px_buffer) {
            height_px_buffer_scalesafe = height_px_buffer;
        }

        // TODO: Maybe just set all of this inside of capture_frame::ready() instead?
        //
        // XXX: We rotate with scranrot instead of set_buffer_transform, for now. See Hyprland #14441
        // wl_surface_set_buffer_transform(
        //     freezeframe->surface.wl_surface,
        //     st_output->transform
        // );
        wp_viewport_set_source(
            freezeframe->subsurface.viewport,
            wl_fixed_from_int(0),
            wl_fixed_from_int(0),
            wl_fixed_from_int(width_px_buffer_scalesafe),
            wl_fixed_from_int(height_px_buffer_scalesafe)
        );
        wp_viewport_set_destination(
            freezeframe->subsurface.viewport,
            parent_surface->width_logical,
            parent_surface->height_logical
        );
        wl_surface_commit(
            freezeframe->subsurface.wl_surface
        );
    }

    freezeframe->subsurface.width_px_buffer  = width_px_buffer;
    freezeframe->subsurface.height_px_buffer = height_px_buffer;
}
