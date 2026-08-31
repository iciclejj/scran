#include "viewporter.h"

#include "state.h"
#include "state-util.h"
#include "capture.h"
#include "ui.h"
#include "cursor.h"
#include "freezeframe.h"
#include "print.h"
#include "selection-surface.h"
#include "selection.h"


void
scran_request_exit()
{
    g_state.exit_requested = true;

    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_output_capture *capture = &st_output->capture;

        enum scran_capture_frame_consumers pending = capture->pending_fullscreen_consumers;
        if (pending) {
            capture->pending_fullscreen_consumers = 0;
            capture->fullscreen_video_pending_audio_disabled = false; // (not really needed)
            capture_fullscreen_end(st_output, pending);
        }

        if (capture->video_stage == SCRAN_VIDEO_STAGE_CAPTURING) {
            capture_video_request_stop(st_output);
        }
    }
}

static inline int32_t
downscale_cosmic_style(
    double to_downscale,
    double normalized_scale
) {
    return round((double)to_downscale / normalized_scale);
}

// See https://github.com/pop-os/cosmic-comp/issues/2240
//   And comment in `handle_fractional_scale_preferred_scale`
static inline double
guess_cosmic_scale_factor(
    struct scran_output_surface *st_surface,
    struct scran_output *st_output
) {
    if (g_state.globals.cosmic_output_manager == NULL
        || !(st_output->fractional_scale_cosmic_1000 || st_output->fractional_scale_wlr)
    ) {
        return 0;
    }

    const double normalized_scale_cosmic = get_normalized_scaler(
        st_output->fractional_scale_cosmic_1000, 1000
    );
    const double normalized_scale_wlr = get_normalized_scaler(
        st_output->fractional_scale_wlr, 256
    );

    const bool have_wlr_only = normalized_scale_wlr && !normalized_scale_cosmic;
    const bool have_cosmic_only = normalized_scale_cosmic && !normalized_scale_wlr;

    if (have_wlr_only) {
        return normalized_scale_wlr;
    } else if (have_cosmic_only) {
        return normalized_scale_cosmic;
    }

    int32_t w_mode = st_output->mode.width_px;
    int32_t h_mode = st_output->mode.height_px;

    int32_t w_fullscreen_surface = st_surface->width_logical;
    int32_t h_fullscreen_surface = st_surface->height_logical;

    if (   w_fullscreen_surface == downscale_cosmic_style(w_mode, normalized_scale_cosmic)
        && h_fullscreen_surface == downscale_cosmic_style(h_mode, normalized_scale_cosmic)
    ) {
        DEBUG("  Guessed COSMIC scaler: %f\n", normalized_scale_cosmic);
        return normalized_scale_cosmic;
    } else if (
           w_fullscreen_surface == downscale_cosmic_style(w_mode, normalized_scale_wlr)
        && h_fullscreen_surface == downscale_cosmic_style(h_mode, normalized_scale_wlr)
    ) {
        DEBUG("  Guessed WLR scaler: %f\n", normalized_scale_wlr);
        return normalized_scale_wlr;
    } else {
        // This should honestly be considered a bug, but I guess better to
        // avoid crashing for something like this.
        eprintf("  WARNING: Could not verify correct scaling factor.\n");
        eprintf("    Guessing COSMIC scaler: %f\n", normalized_scale_cosmic);
        // Default to this since it's what the official cosmic tools use,
        // and has higher precision
        assert(normalized_scale_cosmic); // We shouldn't have arrived here if unset
        return normalized_scale_cosmic;
    }
}

// XXX: This logic is only verified to be correct for wlr-layer-surface
// surfaces (and technically only ones that span the entire screen, though I
// doubt that part would affect anything in here).
double
get_surface_scale_factor_normalized(
    struct scran_output_surface *st_surface
) {
    struct scran_output *st_output = &g_state.outputs[get_containing_output_array_index(st_surface)];

    const double normalized_scale_cosmic = guess_cosmic_scale_factor(st_surface, st_output);
    if (normalized_scale_cosmic) {
        return normalized_scale_cosmic;
    }

    const double normalized_scale_wp = (double)st_surface->fractional_scale_wp_120 / 120.0;
    if (normalized_scale_wp) {
        DEBUG("      getting wp_fractional_scale: %f\n", normalized_scale_wp);
        return normalized_scale_wp;
    }

    DEBUG("      getting wl_output scale: %d\n", st_output->scale);
    return st_output->scale;
}

static inline void
update_surface_scale_and_size(
    struct scran_output_surface *st_surface
) {
    double scale_factor = get_surface_scale_factor_normalized(st_surface);
    DEBUG("      Using scale factor: %f\n", scale_factor);

    st_surface->final_scale_factor_normalized = scale_factor;

    st_surface->width_px_buffer  = round(scale_factor * st_surface->width_logical);
    st_surface->height_px_buffer = round(scale_factor * st_surface->height_logical);
}

void
update_selection_surface_viewport(
    struct scran_output *st_output
) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct scran_output_surface          *st_surface         = &selection_surface->surface;

    // A disabled selection surface may have the 1x1 transparent buffer
    // attached. Don't call this function in that case.
    assert(!selection_surface->disable_reason_mask);

    if (!(st_surface->viewport
          && st_surface->width_px_buffer && st_surface->height_px_buffer
          && st_surface->width_logical   && st_surface->height_logical)
    ) {
        return;
    }

    DEBUG("    Updating selection-surface viewport...\n");
    DEBUG("      src_width: %d, src_height: %d,  dst_width: %d, dst_height: %d\n",
          st_surface->width_px_buffer, st_surface->height_px_buffer,
          st_surface->width_logical, st_surface->height_logical
    );

    wp_viewport_set_source(
        st_surface->viewport,
        wl_fixed_from_int(0),
        wl_fixed_from_int(0),
        wl_fixed_from_int(st_surface->width_px_buffer),
        wl_fixed_from_int(st_surface->height_px_buffer)
    );

    wp_viewport_set_destination(
        st_surface->viewport,
        st_surface->width_logical,
        st_surface->height_logical
    );
}

// NOTE: This does not necessarily force-redraw the buffer, since buffer
// handling, beyond getting/calculating recommended size, is not part of
// scran_output_surface.
void
update_surface_scale_bufsize_viewport(
    struct scran_output *st_output
) {
    struct scran_output_surface *st_surface = &st_output->selection_surface.surface;
    DEBUG("  update_surface_scale_bufsize_viewport()\n");

    DEBUG("    Updating scale and size...\n");
    update_surface_scale_and_size(st_surface);

    if (!st_output->selection_surface.disable_reason_mask) {
        update_selection_surface_viewport(st_output);
    } else {
        DEBUG("    Selection surface disabled; deferring viewport update.\n");
    }

    freezeframe_surface_update_scale_size_viewport(st_output);
}

void
do_scale_updates(struct scran_output *st_output)
{
    update_surface_scale_bufsize_viewport(st_output);
    cursor_reinit(st_output);
    reinit_scran_ui(&st_output->selection_surface.ui_ctx, st_output->selection_surface.surface.final_scale_factor_normalized);
    // Do not update if size_is_frozen, since there might be an active capture.
    if (st_output->selection_ctx.selection_state == SELECTION_NONE && !st_output->selection_ctx.size_is_frozen) {
        selection_set_box_px(
            &st_output->selection_ctx,
            get_selection_surface_pre_selection_box(st_output)
        );
    }
    set_force_redraw_selection_surface_buffers(st_output);
    request_selection_surface_frame_callback(st_output);
}
