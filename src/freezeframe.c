#include <wayland-client-protocol.h>

#include "selection.h"
#include "ui.h"
#include "viewporter.h"

#include "state.h"
#include "state-util.h"
#include "capture.h"
#include "freezeframe.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "print.h"


void
freezeframe_capture_start_assume_callback_set(struct scran_output *st_output)
{
    assert(st_output->freezeframe.callback != NULL);

    struct scran_freezeframe_buffer *capture_buffer = &st_output->freezeframe.capture_buffer;

    if (capture_buffer->busy) { // XXX: Not thread-safe.
        capture_buffer->release_callback = freezeframe_capture_start_assume_callback_set;
        return;
    }

    image_capture_request_frame(
        &st_output->freezeframe.frame_ctx,
        st_output->freezeframe.session.wl_session,
        st_output->freezeframe.capture_buffer.scran_wl_buffer.wl_buffer,
        st_output->freezeframe.session.source_dimensions_px.x,
        st_output->freezeframe.session.source_dimensions_px.y,
        SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME
    );
}

// Use freezeframe_capture_refresh post-init/during normal runtime
void
freezeframe_capture_start(
    struct scran_output *st_output,
    freezeframe_callback callback
) {
    assert(callback != NULL);
    assert(st_output->freezeframe.callback == NULL);

    st_output->freezeframe.callback = callback;
    freezeframe_capture_start_assume_callback_set(st_output);
}

void
freezeframe_hide_surface(struct scran_output *st_output)
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

    // XXX: These should theoretically be set after the commit goes through
    freezeframe->showing = false;
    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_text( SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_ON);
        scran_ui_textline_item_set_color(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_COLOR_DEFAULT);
        request_selection_surface_frame_callback(st_output);
    }
}

// NOTE: This function starts a chain of wayland events that must happen
// strictly sequentially (which is why it is in the form of a chain of events).
// Follow the listeners to see where each step takes you...
//
// Conceptually, we just need to:
//   1    Hide all our surfaces (selection surface, old freezeframe)
//          Prevents them appearing in our captured/"frozen" frame
//   2    Capture the output
//   3.1  Show the capture as our new freezeframe
//   3.2  Restore our selection surface
void
freezeframe_capture_refresh(
    struct scran_output *st_output,
    freezeframe_callback callback
) {
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    if (freezeframe->callback != NULL) {
        eprintf("Freezeframe already in progress.\n");
        return;
    }
    assert(callback != NULL);
    freezeframe->callback = callback;

    // We will have to empty out, and then re-initialize our selection, so that
    // we don't also capture/"freeze" our selection surface. The freezeframe
    // capture_frame::ready handler calls the regular surface init function.

    // Old freezeframe is not necessarily already hidden, since this function
    // can be triggered without releasing focus first.
    freezeframe_hide_surface(st_output);

    hide_selection_surface_then(st_output, &presentation_feedback_listener__selection_transparent_for_freezeframe, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FREEZEFRAME_HIDE);
    freezeframe->unhide_after_capture = true;
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
