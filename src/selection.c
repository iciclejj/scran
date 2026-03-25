#include "state.h"
#include "selection.h"
#include "print.h"
#include "capture.h"
#include "util/blend2d.h"
#include <wayland-client-core.h>


extern struct scran g_state;


void
set_selection_surface_theme(
    struct scran_output *st_output,
    enum surface_theme action
) {
    struct BLRgba32 stroke_style;
    static const double stroke_width = BLCONTEXT_STROKE_WIDTH;

    struct BLRgba32 fill_style;
    static const enum BLFillRule fill_rule = BL_FILL_RULE_EVEN_ODD;

    switch (action) {
    case SURFACE_THEME_DEFAULT:
        stroke_style = BLCONTEXT_RGBA32_STROKE_STYLE_DEFAULT;
        fill_style = BLCONTEXT_RGBA32_FILL_STYLE_DEFAULT;
        break;
    case SURFACE_THEME_VIDEO_CAPTURE:
        stroke_style = BLCONTEXT_RGBA32_STROKE_STYLE_VIDEO_CAPTURE;
        fill_style = BLCONTEXT_RGBA32_FILL_STYLE_VIDEO_CAPTURE;
        break;
    }

    struct scran_output_surface *st_surface = &st_output->surface;

    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];

        bl_context_set_stroke_style_rgba32(&st_buffer->bl_ctx, stroke_style.value);
        bl_context_set_stroke_width(&st_buffer->bl_ctx, stroke_width);

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, fill_style.value);
        bl_context_set_fill_rule(&st_buffer->bl_ctx, fill_rule);
    }

    // XXX HACK: This forces a full redraw inside of capture::frame
    //               TODO: Probably implement a force_redraw flag or similar,
    //               somewhere that capture::frame can easily read it.
    //               (At least once we implement more complicated scenes that
    //               might change the dirty-rect dynamics that we take
    //               advantage of here.)
    st_surface->bl_box_currently_drawn = st_output->selection_ctx.bl_box_bounds;
}


void
signal_selection_initialized(struct scran_output *st_output)
{
    // TODO: Not sure if we should deinvert in here or let the caller decide

    st_output->selection_ctx.selection_state = SELECTION_COMPLETE;

    // Make sure this is initialized immediately, to not be dependent on
    // surface::frame being done, for example when using 'scran -eg'.
    //     TODO: Would be better to de-couple this somehow, or just stop
    //     using capture_area_px, in favor of bl_box_already_drawn.
    st_output->capture.frame_ctx.capture_area_px = get_reverse_transform(
        st_output->selection_ctx.bl_box,
        st_output->mode.width_px,
        st_output->mode.height_px,
        st_output->transform
    );

    if (g_state.options.capture_and_exit_after_selection_init) {
        DEBUG("STARTING AUTOMATIC IMAGE CAPTURE\n");
        start_image_capture(st_output);
        g_state.exit_requested = true;
    }
}


void
start_grabbing_focus()
{
    DEBUG("Grabbing focus\n");

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *st_output = &g_state.outputs[i];
        // NULL sets an infinite region
        wl_surface_set_input_region(st_output->surface.wl_surface, NULL);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            st_output->surface.layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
        );
        wl_surface_commit(st_output->surface.wl_surface);
    }
}

void
stop_grabbing_focus()
{
    DEBUG("Releasing focus\n");

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *st_output = &g_state.outputs[i];
        wl_surface_set_input_region(st_output->surface.wl_surface, g_state.empty_wl_region);
        wl_surface_commit(st_output->surface.wl_surface);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            st_output->surface.layer_surface,
            SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_UNFOCUSED
        );
    }
}

