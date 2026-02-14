#ifndef STATE_UTIL_H
#define STATE_UTIL_H

#include "state.h"


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


#endif
