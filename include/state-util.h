#ifndef STATE_UTIL_H
#define STATE_UTIL_H

#include <assert.h>
#include <wayland-util.h>
#include <math.h>
#include <stddef.h>

#include "viewporter.h"

#include "state.h"
#include "print.h"


extern struct scran g_state;


// XXX: These functions are duplicated in scranrot. Maybe just use scranrot's.
static inline int
get_transformed_height(int src_width, int src_height, enum wl_output_transform transform)
{
    return transform == WL_OUTPUT_TRANSFORM_90
        || transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
        || transform == WL_OUTPUT_TRANSFORM_270
        || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270
         ? src_width
         : src_height;
}
static inline int
get_transformed_width(int src_width, int src_height, enum wl_output_transform transform)
{
    return transform == WL_OUTPUT_TRANSFORM_90
        || transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
        || transform == WL_OUTPUT_TRANSFORM_270
        || transform == WL_OUTPUT_TRANSFORM_FLIPPED_270
         ? src_height
         : src_width;
}

static inline int32_t
get_transformed_output_width(struct scran_output *st_output) {
    return get_transformed_width(st_output->mode.width_px, st_output->mode.height_px, st_output->transform);
}

static inline int32_t
get_transformed_output_height(struct scran_output *st_output) {
    return get_transformed_height(st_output->mode.width_px, st_output->mode.height_px, st_output->transform);
}

static inline void
clamp_to_transformed_output_width(int *val, struct scran_output *st_output)
{
    if (*val < 0) {
        *val = 0;
    } else if (*val > get_transformed_output_width(st_output)) {
        *val = get_transformed_output_width(st_output);
    }
}

static inline void
clamp_to_transformed_output_height(int *val, struct scran_output *st_output)
{
    if (*val < 0) {
        *val = 0;
    } else if (*val > get_transformed_output_height(st_output)) {
        *val = get_transformed_output_height(st_output);
    }
}


static inline int8_t
get_containing_output_array_index(void *ptr) {
    char *ptr_ = ptr;

    for (int i = 0; i < g_state.n_outputs; ++i) {
        if ((char *)(g_state.outputs + i) <= ptr_ && ptr_ < (char *)(g_state.outputs + i + 1)) {
            return i;
        }
    }

    return -1;
}


static inline uint8_t
get_output_array_index(const struct scran_output *st_output) {
    // Assert we're within g_state.output[]
    assert((uintptr_t)st_output  < (uintptr_t)&g_state.outputs + sizeof(g_state)
        && (uintptr_t)st_output >= (uintptr_t)&g_state.outputs
    );

    ptrdiff_t index = st_output - g_state.outputs;

    assert(index >= 0);
    assert(index < MAX_OUTPUTS);
    assert(index <= UINT8_MAX);
    assert(g_state.outputs <= st_output && st_output < (g_state.outputs + MAX_OUTPUTS));

    return (uint8_t)index;
}


static inline void
global_logical_coordinates_to_output_pixel_coordinates(
    struct BLRectI rect_in,
    struct BLRectI *rect_out,
    struct scran_output **containing_output
) {
    *containing_output = NULL;

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output_xdg_geometry *geometry = &g_state.outputs[i].xdg_geometry;

        bool y_within_bounds = rect_in.y >= geometry->y_px
                            && rect_in.y  < geometry->y_px + geometry->height_px;

        bool x_within_bounds = rect_in.x >= geometry->x_px
                            && rect_in.x < geometry->x_px + geometry->width_px;

        if (y_within_bounds && x_within_bounds) {
            *containing_output = &g_state.outputs[i];

            double scale = (*containing_output)->surface.final_scale_factor_normalized;

            rect_out->x = round(scale * (rect_in.x - geometry->x_px));
            rect_out->y = round(scale * (rect_in.y - geometry->y_px));
            rect_out->w = round(scale * (rect_in.w));
            rect_out->h = round(scale * (rect_in.h));

            clamp_to_transformed_output_width(&rect_out->w, *containing_output);
            clamp_to_transformed_output_height(&rect_out->h, *containing_output);

            // TODO: Snap onto max width/height if 1 off, to ensure all edges
            // are reachable?

            return;
        }
    }

    if (*containing_output == NULL) {
        return;
    }
}


// Made for cosmic_output_head and wlr_output_head scalers.
// Essentially wl_fixed_to_double but with any denominator.
static inline double
_get_normalized_scaler(
    int32_t numerator,
    int32_t denominator
) {
    return (double)numerator / (double)denominator;
}

static inline int32_t
_downscale_cosmic_style(
    double to_downscale,
    double normalized_scale
) {
    return round((double)to_downscale / normalized_scale);
}

// See https://github.com/pop-os/cosmic-comp/issues/2240
//   And comment in `handle_fractional_scale_preferred_scale`
static inline double
_guess_cosmic_scale_factor(
    struct scran_output *st_output
) {
    if (g_state.globals.cosmic_output_manager == NULL
        || !(st_output->fractional_scale_cosmic_1000 || st_output->fractional_scale_wlr)
    ) {
        return 0;
    }

    struct scran_output_surface *st_surface = &st_output->surface;

    const double normalized_scale_cosmic = _get_normalized_scaler(
        st_output->fractional_scale_cosmic_1000, 1000
    );
    const double normalized_scale_wlr = _get_normalized_scaler(
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

    if (   w_fullscreen_surface == _downscale_cosmic_style(w_mode, normalized_scale_cosmic)
        && h_fullscreen_surface == _downscale_cosmic_style(h_mode, normalized_scale_cosmic)
    ) {
        DEBUG("  Guessed COSMIC scaler: %f\n", normalized_scale_cosmic);
        return normalized_scale_cosmic;
    } else if (
           w_fullscreen_surface == _downscale_cosmic_style(w_mode, normalized_scale_wlr)
        && h_fullscreen_surface == _downscale_cosmic_style(h_mode, normalized_scale_wlr)
    ) {
        DEBUG("  Guessed WLR scaler: %f\n", normalized_scale_wlr);
        return normalized_scale_wlr;
    } else {
        // This should honestly be considered a bug, but I guess better to
        // avoid crashing for something like this.
        eprintf("  Warning: Could not verify correct scaling factor.\n");
        eprintf("    Guessing COSMIC scaler: %f\n", normalized_scale_cosmic);
        // Default to this since it's what the official cosmic tools use,
        // and has higher precision
        assert(normalized_scale_cosmic); // We shouldn't have arrived here if unset
        return normalized_scale_cosmic;
    }
}

static inline double
get_surface_scale_factor_normalized(
    struct scran_output_surface *st_surface
) {
    struct scran_output *st_output = wl_container_of(st_surface, st_output, surface);

    DEBUG("get_surface_scale_factor_normalized()\n");

    const double normalized_scale_cosmic = _guess_cosmic_scale_factor(st_output);
    if (normalized_scale_cosmic) {
        return normalized_scale_cosmic;
    }

    const double normalized_scale_wp = (double)st_surface->fractional_scale_wp_120 / 120.0;
    if (normalized_scale_wp) {
        DEBUG("  getting wp_fractional_scale: %f\n", normalized_scale_wp);
        return normalized_scale_wp;
    }

    DEBUG("  getting wl_output scale: %d\n", st_output->scale);
    return st_output->scale;
}

// TODO: Merge this with update_selection_surface_viewport? Doesn't really make
// sense to call them separately other than during init, and we already have a
// bunch of safeguards in _viewport.
static inline void
update_selection_surface_scale_and_size(
    struct scran_output *st_output
) {
    DEBUG("update_selection_surface_scale_and_size()\n");

    double scale_factor = get_surface_scale_factor_normalized(&st_output->surface);
    DEBUG("  Using scale factor: %f\n", scale_factor);

    st_output->surface.final_scale_factor_normalized = scale_factor;

    st_output->surface.width_px_buffer  = round(scale_factor * st_output->surface.width_logical);
    st_output->surface.height_px_buffer = round(scale_factor * st_output->surface.height_logical);
}

static inline void
update_selection_surface_viewport(
    struct scran_output *st_output
) {
    // TODO: Maybe move this responsibility into output::scale etc, so we're
    // not forced to do this check every time we update
    if (st_output->surface.viewport == NULL) {
        return;
    }
    if (!(st_output->surface.width_px_buffer && st_output->surface.height_px_buffer)) {
        return;
    }
    if (!(st_output->surface.width_logical && st_output->surface.height_logical)) {
        return;
    }

    DEBUG("update_selection_surface_viewport():"
          "  src_width: %d, src_height: %d,  dst_width: %d, dst_height: %d\n",
          st_output->surface.width_px_buffer, st_output->surface.height_px_buffer,
          st_output->surface.width_logical, st_output->surface.height_logical
    );

    if (st_output->surface.width_px_buffer && st_output->surface.height_px_buffer) {
        wp_viewport_set_source(
            st_output->surface.viewport,
            wl_fixed_from_int(0),
            wl_fixed_from_int(0),
            wl_fixed_from_int(st_output->surface.width_px_buffer),
            wl_fixed_from_int(st_output->surface.height_px_buffer)
        );
    }

    wp_viewport_set_destination(
        st_output->surface.viewport,
        st_output->surface.width_logical,
        st_output->surface.height_logical
    );

    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        st_output->surface.double_buffer[i].force_redraw = true;
    }
}


#endif
