#ifndef STATE_UTIL_H
#define STATE_UTIL_H

#include <assert.h>

#include "state.h"
#include "stddef.h"


extern struct scran g_state;


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
get_output_width_logical(struct scran_output *st_output) {
    return get_transformed_width(st_output->mode.width_px, st_output->mode.height_px, st_output->transform);
}

static inline int32_t
get_output_height_logical(struct scran_output *st_output) {
    return get_transformed_height(st_output->mode.width_px, st_output->mode.height_px, st_output->transform);
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


#endif
