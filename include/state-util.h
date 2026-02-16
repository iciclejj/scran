#ifndef STATE_UTIL_H
#define STATE_UTIL_H

#include <assert.h>

#include "state.h"
#include "stddef.h"

extern struct scran g_state;


static inline int32_t
get_output_width_logical(struct scran_output *st_output) {
    return st_output->transform == WL_OUTPUT_TRANSFORM_90
        || st_output->transform == WL_OUTPUT_TRANSFORM_270
         ? st_output->mode.height_px
         : st_output->mode.width_px;
}

static inline int32_t
get_output_height_logical(struct scran_output *st_output) {
    return st_output->transform == WL_OUTPUT_TRANSFORM_90
        || st_output->transform == WL_OUTPUT_TRANSFORM_270
         ? st_output->mode.width_px
         : st_output->mode.height_px;
}

static inline uint8_t
get_output_array_index(const struct scran_output *st_output) {
    ptrdiff_t index = st_output - g_state.outputs;

    assert(index >= 0);
    assert(index < MAX_OUTPUTS);
    assert(index <= UINT8_MAX);
    assert(g_state.outputs <= st_output && st_output < (g_state.outputs + MAX_OUTPUTS));

    return (uint8_t)index;
}


#endif
