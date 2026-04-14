#include <wayland-client.h>

#include "state.h"
#include "state-util.h"
#include "selection.h"
#include "print.h"
#include "capture.h"
#include "surface__selection.h"
#include "util/blend2d.h"


extern struct scran g_state;


void
set_selection_surface_theme(
    struct scran_output *st_output,
    enum surface_theme action
) {
    struct BLRgba32 fill_style;
    static const enum BLFillRule fill_rule = BL_FILL_RULE_EVEN_ODD;

    switch (action) {
    case SURFACE_THEME_DEFAULT:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_DEFAULT;
        break;
    case SURFACE_THEME_VIDEO_CAPTURE:
        fill_style = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE;
        break;
    }

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &st_output->selection_surface.double_buffer[i];

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, fill_style.value);
        bl_context_set_fill_rule(&st_buffer->bl_ctx, fill_rule);

        st_buffer->force_redraw = true;
    }

    request_selection_surface_update(st_output);
}


// TODO: Rename to set_selection_state_complete
void
set_selection_initialized(struct scran_output *st_output)
{
    assert(st_output->selection_ctx.selection_state == SELECTION_INITIALIZING
           || g_state.options.have_custom_initial_selection && st_output->selection_ctx.selection_state == SELECTION_NONE);

    // TODO: Not sure if we should deinvert in here or let the caller decide

    st_output->selection_ctx.selection_state = SELECTION_COMPLETE;

    // Reset button, since this function could have interrupted an ongoing
    // state-dependent action.
    g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;

    // Make sure this is initialized immediately, to not be dependent on
    // surface::frame being done, for example when using 'scran -eg'.
    //     TODO: Would be better to de-couple this somehow, or just stop
    //     using capture_area_px, in favor of bl_box_already_drawn.
    st_output->capture.frame_ctx.capture_area_px = get_reverse_transform(
        st_output->selection_ctx.box_px,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );

    assert(st_output->capture.frame_ctx.capture_area_px.x1 <= get_transformed_output_width(st_output));
    assert(st_output->capture.frame_ctx.capture_area_px.y1 <= get_transformed_output_height(st_output));

    if (g_state.options.capture_and_exit_after_selection_init) {
        DEBUG("STARTING AUTOMATIC IMAGE CAPTURE\n");
        request_image_capture(st_output);
        g_state.exit_requested = true;
    }
}

void
unset_selection_freeze_size(struct scran_output *st_output)
{
    enum selection_state *selection_state = &st_output->selection_ctx.selection_state;

    switch(*selection_state) {
        case SELECTION_COMPLETE_FREEZE_SIZE: *selection_state = SELECTION_COMPLETE; break;
        case SELECTION_REBASING_FREEZE_SIZE: *selection_state = SELECTION_REBASING; break;
        default:
            assert("UNEXPECTED: unset_selection_freeze() called without frozen selection state");
            break;
    }
}

bool
set_selection_freeze_size(struct scran_output *st_output)
{
    enum selection_state *selection_state = &st_output->selection_ctx.selection_state;
    switch(*selection_state) {
        case SELECTION_REBASING: *selection_state = SELECTION_REBASING_FREEZE_SIZE; break;
        case SELECTION_COMPLETE: *selection_state = SELECTION_COMPLETE_FREEZE_SIZE; break;
        case SELECTION_RESIZING:
            // Incompatible state; neutralize button.
            g_state.seat.pointer_ctx.active_button = SCRAN_BTN_NONE;
            *selection_state = SELECTION_COMPLETE_FREEZE_SIZE; break;
        default:
            eprintf("Can't freeze selection size in current selection state. (SELECTION_STATE=%d)\n", *selection_state);
            return false;
    }

    return true;
}

void
start_grabbing_focus()
{
    DEBUG("Grabbing focus\n");

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *st_output = &g_state.outputs[i];
        struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

        // NULL sets an infinite region
        wl_surface_set_input_region(st_surface->wl_surface, NULL);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            st_surface->layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
        );
        wl_surface_commit(st_surface->wl_surface);
    }
}

void
stop_grabbing_focus()
{
    DEBUG("Releasing focus\n");

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *st_output = &g_state.outputs[i];
        struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

        wl_surface_set_input_region(st_surface->wl_surface, g_state.empty_wl_region);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            st_surface->layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED
        );
        wl_surface_commit(st_surface->wl_surface);
    }
}

