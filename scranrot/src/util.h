#ifndef SCRANROT_UTIL_H
#define SCRANROT_UTIL_H


#include "scranrot.h"


#ifndef SCRANROT_ASSERT
#include <assert.h>
#define SCRANROT_ASSERT assert
#endif

#define SCRANROT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define SCRANROT_UNLIKELY(x) __builtin_expect(!!(x), 0)


// load unaligned
static inline uint32_t
scranrot_loadu_u32(const void *src)
{
    uint32_t val;
    __builtin_memcpy(&val, src, sizeof(val));
    return val;
}

// store unaligned
static inline void
scranrot_storeu_u32(void *dst, uint32_t val)
{
    __builtin_memcpy(dst, &val, sizeof(val));
}


static inline int
scranrot_get_transformed_height(int src_width, int src_height, enum scranrot_transform transform)
{
    return transform == SCRANROT_TRANSFORM_90
        || transform == SCRANROT_TRANSFORM_FLIPPED_90
        || transform == SCRANROT_TRANSFORM_270
        || transform == SCRANROT_TRANSFORM_FLIPPED_270
         ? src_width
         : src_height;
}

static inline int
scranrot_get_transformed_width(int src_width, int src_height, enum scranrot_transform transform)
{
    return transform == SCRANROT_TRANSFORM_90
        || transform == SCRANROT_TRANSFORM_FLIPPED_90
        || transform == SCRANROT_TRANSFORM_270
        || transform == SCRANROT_TRANSFORM_FLIPPED_270
         ? src_height
         : src_width;
}


#endif
