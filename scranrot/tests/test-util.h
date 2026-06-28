#ifndef SCRANROT_TEST_UTIL_H
#define SCRANROT_TEST_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scranrot.h"

#include "./test-types.h"

static inline const char *
scranrot_test_transform_name(enum scranrot_transform transform)
{
    switch (transform) {
    case SCRANROT_TRANSFORM_NORMAL:
        return "normal";
    case SCRANROT_TRANSFORM_90:
        return "90";
    case SCRANROT_TRANSFORM_180:
        return "180";
    case SCRANROT_TRANSFORM_270:
        return "270";
    case SCRANROT_TRANSFORM_FLIPPED:
        return "flipped";
    case SCRANROT_TRANSFORM_FLIPPED_90:
        return "flipped-90";
    case SCRANROT_TRANSFORM_FLIPPED_180:
        return "flipped-180";
    case SCRANROT_TRANSFORM_FLIPPED_270:
        return "flipped-270";
    default:
        return "invalid";
    }
}

static inline void *
scranrot_test_xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "out of memory allocating %zu bytes\n", size);
        abort();
    }
    return ptr;
}

static inline uint32_t
update_rng_deterministically(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline void
scranrot_test_generate_test_image(
    uint8_t *buf,
    size_t buf_size,
    const struct scranrot_test_dimensions dimensions,
    int stride_bytes,
    uint32_t seed
) {
    memset(buf, 0xa5, buf_size);

    uint32_t rng = seed;

    for (int y = 0; y < dimensions.height_px; ++y) {
        for (int x = 0; x < dimensions.width_px; ++x) {

            uint8_t *const px = buf + (y * stride_bytes) + (x * RGBA32_PIXEL_STRIDE);

            px[0] = (uint8_t)update_rng_deterministically(&rng);
            px[1] = (uint8_t)update_rng_deterministically(&rng);
            px[2] = (uint8_t)update_rng_deterministically(&rng);
            px[3] = (uint8_t)update_rng_deterministically(&rng);
        }
    }
}

static inline bool
scranrot_test_compare_bytes(
    const uint8_t *got,
    const uint8_t *reference,
    size_t         size,
    const char    *buffer_name
) {
    for (size_t i = 0; i < size; ++i) {
        if (got[i] != reference[i]) {
            fprintf(
                stderr, "  %s mismatch at byte %zu: got %u, reference %u\n",
                buffer_name, i, (unsigned)got[i], (unsigned)reference[i]
            );
            return false;
        }
    }

    return true;
}

static inline bool
scranrot_test_cpu_has_ssse3(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("ssse3");
#else
    return false;
#endif
}

static inline bool
scranrot_test_cpu_has_avx2(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

#endif
