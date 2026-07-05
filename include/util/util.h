#ifndef SCRAN_UTIL_H
#define SCRAN_UTIL_H


#include <stdbool.h>
#include <stddef.h>

#include <wayland-client-protocol.h>


#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(*arr))


bool scran_full_write(int fd, const char *src, size_t n_bytes);

static inline bool
wl_output_transform_is_flipped(enum wl_output_transform transform) {
    return transform >= WL_OUTPUT_TRANSFORM_FLIPPED;
}

static inline enum wl_output_transform
wl_output_transform_without_flip(enum wl_output_transform transform) {
    return wl_output_transform_is_flipped(transform) ? (transform - WL_OUTPUT_TRANSFORM_FLIPPED) : transform;
}


#endif
