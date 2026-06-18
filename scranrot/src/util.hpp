#ifndef SCRANROT_UTIL_H
#define SCRANROT_UTIL_H


#include "../include/scranrot.h"


#ifndef SCRANROT_ASSERT
#include <cassert>
#define SCRANROT_ASSERT assert
#endif

#define SCRANROT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SCRANROT_UNLIKELY(x) __builtin_expect(!!(x), 0)


namespace scranrot::internal {

    static inline int
    get_transformed_height(int src_width, int src_height, enum scranrot_transform transform)
    {
        return transform == SCRANROT_TRANSFORM_90
            || transform == SCRANROT_TRANSFORM_FLIPPED_90
            || transform == SCRANROT_TRANSFORM_270
            || transform == SCRANROT_TRANSFORM_FLIPPED_270
             ? src_width
             : src_height;
    }

    static inline int
    get_transformed_width(int src_width, int src_height, enum scranrot_transform transform)
    {
        return transform == SCRANROT_TRANSFORM_90
            || transform == SCRANROT_TRANSFORM_FLIPPED_90
            || transform == SCRANROT_TRANSFORM_270
            || transform == SCRANROT_TRANSFORM_FLIPPED_270
             ? src_height
             : src_width;
    }

}


#endif
