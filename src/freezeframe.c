#include <wayland-client-protocol.h>

#include "ext-image-copy-capture-v1.h"
#include "ui.h"
#include "viewporter.h"

#include "state.h"
#include "state-util.h"
#include "capture.h"
#include "init.h"
#include "freezeframe.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "print.h"


extern struct scran g_state;


void
freezeframe_capture_start_assume_callback_set(struct scran_output *st_output) {
    assert(st_output->freezeframe.callback != NULL);

    struct scran_freezeframe_buffer *capture_buffer = &st_output->freezeframe.capture_buffer;

    if (capture_buffer->busy) { // XXX: Not thread-safe.
        capture_buffer->release_callback = freezeframe_capture_start_assume_callback_set;
        return;
    }

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(
            st_output->freezeframe.wl_capture_session
        );

    ext_image_copy_capture_frame_v1_attach_buffer(
        frame,
        capture_buffer->scran_wl_buffer.wl_buffer
    );
    ext_image_copy_capture_frame_v1_damage_buffer(
        frame,
        0, 0, st_output->mode.width_px, st_output->mode.height_px
    );
    ext_image_copy_capture_frame_v1_add_listener(
        frame,
        &image_copy_capture_frame_listener__freezeframe,
        st_output
    );
    ext_image_copy_capture_frame_v1_capture(frame);

    // Force some output damage, since some compositors (like Hyprland on rapid
    // consecutive freezeframe refreshes) may wait indefinitely for the next
    // capture frame if no damage is detected.
    capture_force_next_frame(st_output);
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
//          Must be done after 3.1, since they both use the same layer/z-index
void
freezeframe_capture_refresh(
    struct scran_output *st_output,
    freezeframe_callback callback
) {
    struct scran_output_freezeframe      *freezeframe       = &st_output->freezeframe;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

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
    // Once the ::presented event has verified that the selection surface was
    // hidden, we start the capture from within there.
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, selection_surface->surface.wl_surface),
        &presentation_feedback_listener__selection_transparent_for_freezeframe,
        st_output
    );
    freezeframe_hide_selection_surface(st_output);
    freezeframe->unhide_after_capture = true;
    // Need to prevent any new or in-flight frame callbacks from cancelling out
    // our surface hiding
    selection_surface->frame_callbacks_disabled = true;
}

void
freezeframe_hide_surface(struct scran_output *st_output)
{
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

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
        scran_ui_textline_item_set_text(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline_keymap), SCRAN_UI_STATUSLINE_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_TEXT_STATUSLINE_KEYMAP_FREEZEFRAME_TURN_ON);
        scran_ui_textline_item_set_color(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_statusline_keymap), SCRAN_UI_STATUSLINE_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_COLOR_DEFAULT);
        request_selection_surface_frame_callback(st_output);
    }
}

void
freezeframe_hide_selection_surface(struct scran_output *st_output)
{
    struct scran_output_surface     *st_surface  = &st_output->selection_surface.surface;
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    assert(SURFACE_SHM_FORMAT == WL_SHM_FORMAT_ARGB8888); // Alpha channel must not be ignored.
    wl_surface_attach(
        st_surface->wl_surface,
        freezeframe->transparent_single_pixel_buffer.scran_wl_buffer.wl_buffer, 0, 0
    );
    wp_viewport_set_source(
        st_surface->viewport,
        wl_fixed_from_int(0), wl_fixed_from_int(0), wl_fixed_from_int(1), wl_fixed_from_int(1)
    );
    wl_surface_damage_buffer(
        st_surface->wl_surface,
        0, 0, 1, 1
    );
    wl_surface_commit(
        st_surface->wl_surface
    );
}

void
freezeframe_unhide_selection_surface(
    struct scran_output *st_output
) {
    struct scran_output_freezeframe      *freezeframe       = &st_output->freezeframe;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    (void)freezeframe;


    // TODO: Get a free buffer instead, and handle the case where can't?
    //         See wl_surface::get_release() (as of wayland 1.25.0, 2026-03-19).
    struct scran_output_selectionSurface_buffer *selection_buffer = &selection_surface->double_buffer[0];

    // Need to attach a correctly-sized buffer back again before re-setting
    // the viewport.
    wl_surface_attach(
        selection_surface->surface.wl_surface,
        selection_buffer->scran_wl_buffer.wl_buffer,
        0, 0
    );
    selection_buffer->busy = true;
    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer
    );
    // Make sure the viewport is set appropriately. The (re-)freezeframe
    // pipeline sets it to 1x1 for the transparent buffer.
    //   TODO: Revisit the postmem init functions now and maybe call
    //   update_surface_scale_bufsize_viewport() here instead.
    wp_viewport_set_source(
        selection_surface->surface.viewport,
        wl_fixed_from_int(0),
        wl_fixed_from_int(0),
        wl_fixed_from_int(selection_surface->surface.width_px_buffer),
        wl_fixed_from_int(selection_surface->surface.height_px_buffer)
    );
    set_force_redraw_selection_surface_buffers(st_output);
    // XXX: This commit is currently redundant in practice, but keeping it here
    // so this function makes more sense on its own.
    //
    // TODO: Refactor the entire freezeframe_capture_refresh() chain so that we
    // avoid all the redundant commits. Maybe move the hiding/unhiding
    // responsibility out of any freezeframe.c function entirely, and have the
    // caller ensure pre/post-recapture state like this manually.
    wl_surface_commit(selection_surface->surface.wl_surface);
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
