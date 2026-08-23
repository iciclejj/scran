#include <assert.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "presentation-time.h"
#include "viewporter.h"

#include "state.h"
#include "state-util.h"
#include "cursor.h"
#include "selection.h"
#include "print.h"
#include "capture.h"
#include "selection-surface.h"
#include "freezeframe.h"
#include "init.h"


void
set_selection_surface_theme(
    struct scran_output *st_output,
    enum surface_theme theme
) {
    struct BLRgba32 fill_style;
    static const enum BLFillRule fill_rule = BL_FILL_RULE_EVEN_ODD;

    switch (theme) {
    case SURFACE_THEME_PRE_SELECTION:
        // Alpha channel must be respected for invisibility.
        assert(SURFACE_SHM_FORMAT_BL == BL_FORMAT_PRGB32);
        fill_style = SCRAN_SELECTION_BORDER_COLOR_INVISIBLE;
        break;
    case SURFACE_THEME_DEFAULT:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_DEFAULT;
        break;
    case SURFACE_THEME_VIDEO_CAPTURE:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE;
        break;
    default:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_DEFAULT;
        break;
    }
    st_output->selection_surface.theme = theme;

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &st_output->selection_surface.double_buffer[i];

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, fill_style.value);
        bl_context_set_fill_rule(&st_buffer->bl_ctx, fill_rule);
    }

    // We need to force full redraw for the colors to change regardless of dirty
    // rects etc.
    set_force_redraw_selection_surface_buffers(st_output);
}


// TODO: Rename to set_selection_state_complete
void
set_selection_initialized(struct scran_output *st_output)
{
    assert(st_output->selection_ctx.selection_state == SELECTION_INITIALIZING
           || (g_state.options.have_custom_initial_selection && st_output->selection_ctx.selection_state == SELECTION_NONE));

    st_output->selection_ctx.selection_state = SELECTION_COMPLETE;

    // Reset button, since this function could have interrupted an ongoing
    // state-dependent action.
    g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;

    // Make sure this is initialized immediately, to not be dependent on
    // surface::frame being done, for example when using 'scran -eg'.
    //     TODO: Would be better to de-couple this somehow.
    capture_update_selection(st_output, selection_get_box_px(&st_output->selection_ctx));

    if (g_state.options.capture_and_exit_after_selection_init) {
        DEBUG("STARTING AUTOMATIC IMAGE CAPTURE\n");
        image_capture_start(st_output, true);
        scran_request_exit();
    }
}

bool
set_selection_freeze_size(struct scran_output *st_output)
{
    enum selection_state *selection_state = &st_output->selection_ctx.selection_state;
    switch(*selection_state) {
        case SELECTION_NONE_FREEZE_SIZE:
        case SELECTION_REBASING_FREEZE_SIZE:
        case SELECTION_COMPLETE_FREEZE_SIZE:
            break;
        case SELECTION_NONE:
            assert(st_output->capture.frame_ctx.fullscreen_capture == true);
            *selection_state = SELECTION_NONE_FREEZE_SIZE;
            break;
        case SELECTION_REBASING:
            *selection_state = SELECTION_REBASING_FREEZE_SIZE;
            break;
        case SELECTION_COMPLETE:
            *selection_state = SELECTION_COMPLETE_FREEZE_SIZE;
            break;
        case SELECTION_RESIZING:
            // Incompatible state; neutralize button.
            g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;
            *selection_state = SELECTION_COMPLETE_FREEZE_SIZE;
            break;
        default:
            eprintf("Can't freeze selection size in current selection state. (SELECTION_STATE=%d)\n", *selection_state);
            return false;
    }

    return true;
}

void
unset_selection_freeze_size(struct scran_output *st_output)
{
    enum selection_state *selection_state = &st_output->selection_ctx.selection_state;

    switch(*selection_state) {
        case SELECTION_NONE_FREEZE_SIZE:     *selection_state = SELECTION_NONE;     break;
        case SELECTION_COMPLETE_FREEZE_SIZE: *selection_state = SELECTION_COMPLETE; break;
        case SELECTION_REBASING_FREEZE_SIZE: *selection_state = SELECTION_REBASING; break;
        default:
            break;
    }
}

static void
hide_selection_surface(struct scran_output *st_output)
{
    struct scran_output_surface *st_surface  = &st_output->selection_surface.surface;

    assert(SURFACE_SHM_FORMAT == WL_SHM_FORMAT_ARGB8888); // Alpha channel must not be ignored.
    wl_surface_attach(
        st_surface->wl_surface,
        g_state.transparent_single_pixel_buffer.wl_buffer, 0, 0
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
hide_selection_surface_then(
    struct scran_output *st_output,
    struct wp_presentation_feedback_listener *listener,
    enum scran_selection_surface_disable_reason reason
) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    // Once the ::presented event has verified that the selection surface was
    // hidden, we start the capture from within there.
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, selection_surface->surface.wl_surface),
        listener,
        st_output
    );

    // Need to prevent any new or in-flight frame callbacks from cancelling out
    // our surface hiding
    selection_surface->disable_reason_mask |= reason;

    hide_selection_surface(st_output);
}

static inline void
unhide_selection_surface(struct scran_output *st_output) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
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
    update_selection_surface_viewport(st_output);
    selection_buffer->busy = true;
    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer
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

    // Scale-triggered callback requests are suppressed while hidden. Ensure
    // the forced redraw above is eventually presented after unhiding.
    // TODO: Make this not double-commit with the above commit.
    request_selection_surface_frame_callback(st_output);
}

void
release_selection_surface_hide(struct scran_output *st_output, enum scran_selection_surface_disable_reason reason)
{
    st_output->selection_surface.disable_reason_mask &= ~reason;
    if (!st_output->selection_surface.disable_reason_mask) {
        unhide_selection_surface(st_output);
    }
}

// We need an output-specific function since freezeframe will need to call back
// into it from the output-specific capture_frame::ready handler.
void
start_grabbing_focus_for_output(
    struct scran_output *st_output
) {
    struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

    DEBUG("start_grabbing_focus_for_output()\n");

    // NULL sets an infinite region
    wl_surface_set_input_region(st_surface->wl_surface, NULL);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_surface->layer_surface,
        SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
    );
    wl_surface_commit(st_surface->wl_surface);

    // TODO: Make arm_selection_surface_frame_callback externally callable, so
    // that we avoid potential double-commit here?
    request_selection_surface_frame_callback(st_output);
}

void
start_grabbing_focus()
{
    DEBUG("Grabbing focus\n");

    FOR_EACH_OUTPUT(i, st_output) {
        start_grabbing_focus_for_output(st_output);
    }
}

void
stop_grabbing_focus()
{
    DEBUG("Releasing focus\n");

    FOR_EACH_OUTPUT(i, st_output) {
        freezeframe_hide_surface(st_output);
    }

    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

        wl_surface_set_input_region(st_surface->wl_surface, g_state.empty_wl_region);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            st_surface->layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED
        );
        wl_surface_commit(st_surface->wl_surface);
    }
}
