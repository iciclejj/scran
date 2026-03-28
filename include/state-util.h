#ifndef STATE_UTIL_H
#define STATE_UTIL_H

#include <assert.h>

#include "state.h"
#include "stddef.h"


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
clamp_to_output_width_logical(int *val, struct scran_output *st_output)
{
    if (*val < 0) {
        *val = 0;
    } else if (*val > get_transformed_output_width(st_output)) {
        *val = get_transformed_output_width(st_output);
    }
}

static inline void
clamp_to_output_height_logical(int *val, struct scran_output *st_output)
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


// containing_output is set to NULL if no outputs contain the coordinates.
static inline void
_global_coordinates_to_output_coordinates(
    int x_in,
    int y_in,
    int *x_out,
    int *y_out,
    struct scran_output **containing_output
) {
    *containing_output = NULL;

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output_xdg_geometry *geometry = &g_state.outputs[i].xdg_geometry;

        bool y_within_bounds = y_in >= geometry->y_px
                            && y_in  < geometry->y_px + geometry->height_px;

        bool x_within_bounds = x_in >= geometry->x_px
                            && x_in  < geometry->x_px + geometry->width_px;

        if (y_within_bounds && x_within_bounds) {
            *containing_output = &g_state.outputs[i];
            *x_out = x_in - geometry->x_px;
            *y_out = y_in - geometry->y_px;
            return;
        }
    }
}

static inline void
global_rect_to_output_box_clamped(
    struct BLRectI global_rect,
    struct BLBoxI *output_box,
    struct scran_output **containing_output
) {
    _global_coordinates_to_output_coordinates(
        global_rect.x,   global_rect.y,
        &output_box->x0, &output_box->y0, containing_output
    );

    if (*containing_output == NULL) {
        return;
    }

    output_box->x1 = output_box->x0 + global_rect.w;
    output_box->y1 = output_box->y0 + global_rect.h;

    clamp_to_output_width_logical(&output_box->x1, *containing_output);
    clamp_to_output_height_logical(&output_box->y1, *containing_output);
}


#endif
