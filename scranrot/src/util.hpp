#ifndef SCRANROT_UTIL_H
#define SCRANROT_UTIL_H


#include "../include/scranrot.h"
#include "./backends.hpp"


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

    template<typename T>
    SCRANROT_ALWAYS_INLINE
    static inline T
    load_unaligned(const void *src)
    {
        static_assert(__is_trivially_copyable(T));
        T val;
        __builtin_memcpy(&val, src, sizeof(val));
        return val;
    }

    template<typename T>
    SCRANROT_ALWAYS_INLINE
    static inline void
    store_unaligned(void *dst, const T &val)
    {
        static_assert(__is_trivially_copyable(T));
        __builtin_memcpy(dst, &val, sizeof(val));
    }

}


#endif
