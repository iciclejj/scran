#ifndef SCRAN_STATE_UTIL_H
#define SCRAN_STATE_UTIL_H

#include <assert.h>
#include <wayland-util.h>
#include <math.h>
#include <stddef.h>

#include "state.h"


extern struct scran g_state;


#define FOR_EACH_OUTPUT(i, varname) \
    for (uint32_t i = (assert(g_state.n_outputs <= MAX_OUTPUTS), 0); i < g_state.n_outputs; ++i) \
        for (struct scran_output *varname = &g_state.outputs[i]; varname; varname = NULL)


void update_surface_scale_bufsize_viewport(struct scran_output *st_output);
double get_surface_scale_factor_normalized(struct scran_output_surface *st_surface);
void do_scale_updates(struct scran_output *st_output);

// Made for cosmic_output_head and wlr_output_head scalers.
// Essentially wl_fixed_to_double but with any denominator.
static inline double
get_normalized_scaler(
    int32_t numerator,
    int32_t denominator
) {
    return (double)numerator / (double)denominator;
}

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

// Just for clarifying intent
static inline int
get_reverse_transformed_width(int src_width, int src_height, enum wl_output_transform transform) {
    return get_transformed_width(src_width, src_height, transform);
}
static inline int
get_reverse_transformed_height(int src_width, int src_height, enum wl_output_transform transform) {
    return get_transformed_height(src_width, src_height, transform);
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

    FOR_EACH_OUTPUT(i, st_output) {
        if ((char *)st_output <= ptr_ && ptr_ < (char *)(st_output + 1)) {
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
global_logical_rect_to_selection(
    BLRectI rect_in,
    BLRectI *rect_out,
    struct scran_output **containing_output
) {
    *containing_output = NULL;

    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_output_xdg_geometry *geometry = &st_output->xdg_geometry;

        bool y_within_bounds = rect_in.y >= geometry->y_logical
                            && rect_in.y  < geometry->y_logical + geometry->h_logical;

        bool x_within_bounds = rect_in.x >= geometry->x_logical
                            && rect_in.x  < geometry->x_logical + geometry->w_logical;

        if (y_within_bounds && x_within_bounds) {
            *containing_output = st_output;

            // XXX: Uses selection-surface as source of truth for output-global scale
            //      TODO: See if there's a better way to do this, e.g. getting scale
            //      from the xdg-output (doesn't seem possible other than estimating
            //      based on logical vs mode res), or verify that every layer surface
            //      on an output will always have the same scale, and unify the scale
            //      factor state variable to be shared across all surfaces on an
            //      output (probably with selection surface as source of truth).
            //      (Yes, as of this comment's commit, we only have one surface per
            //      output anyways, but preparing to add more.)
            double scale = (*containing_output)->selection_surface.surface.final_scale_factor_normalized;

            *rect_out = (BLRectI) {
                .x = round(scale * (rect_in.x - geometry->x_logical)),
                .y = round(scale * (rect_in.y - geometry->y_logical)),
                .w = round(scale * (rect_in.w)),
                .h = round(scale * (rect_in.h)),
            };

            clamp_to_transformed_output_width( &rect_out->w, *containing_output);
            clamp_to_transformed_output_height(&rect_out->h, *containing_output);

            // TODO: Snap onto max width/height if 1 off, to ensure all edges
            // are reachable?

            return;
        }
    }
}


#endif
