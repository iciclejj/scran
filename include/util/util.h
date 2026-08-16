#ifndef SCRAN_UTIL_H
#define SCRAN_UTIL_H


#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <uchar.h>
#include <sys/types.h>

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

static inline void
advance_itoa_7(
    int integer,
    char *restrict ascii,
    ssize_t *restrict i_ascii
) {
    assert(integer <= 9999999);

    int lo4     = integer % 10000;
    int hi3     = integer / 10000;

    int lo4_lo2 = lo4 % 100;
    int lo4_hi2 = lo4 / 100;

    int hi3_lo2 = hi3 % 100;
    int hi3_hi1 = hi3 / 100;

    ascii[(*i_ascii)++] = '0' + hi3_hi1;
    ascii[(*i_ascii)++] = '0' + hi3_lo2 / 10;
    ascii[(*i_ascii)++] = '0' + hi3_lo2 % 10;
    ascii[(*i_ascii)++] = '0' + lo4_hi2 / 10;
    ascii[(*i_ascii)++] = '0' + lo4_hi2 % 10;
    ascii[(*i_ascii)++] = '0' + lo4_lo2 / 10;
    ascii[(*i_ascii)++] = '0' + lo4_lo2 % 10;
}

static inline void
advance_itoa_6(
    int integer,
    char *restrict ascii,
    ssize_t *restrict i_ascii
) {
    assert(integer <= 999999);

    int lo2 = integer % 100;
    int hi4 = integer / 100;

    int hi4_lo = hi4 % 100;
    int hi4_hi = hi4 / 100;

    ascii[(*i_ascii)++] = '0' + hi4_hi / 10;
    ascii[(*i_ascii)++] = '0' + hi4_hi % 10;
    ascii[(*i_ascii)++] = '0' + hi4_lo / 10;
    ascii[(*i_ascii)++] = '0' + hi4_lo % 10;
    ascii[(*i_ascii)++] = '0' + lo2 / 10;
    ascii[(*i_ascii)++] = '0' + lo2 % 10;
}

static inline void
advance_itoa_5(
    int integer,
    char *restrict ascii,
    ssize_t *restrict i_ascii
) {
    assert(integer <= 99999);

    int lo2 = integer % 100;
    int hi4 = integer / 100;

    int hi4_lo = hi4 % 100;
    int hi4_hi = hi4 / 100;

    ascii[(*i_ascii)++] = '0' + hi4_hi;
    ascii[(*i_ascii)++] = '0' + hi4_lo / 10;
    ascii[(*i_ascii)++] = '0' + hi4_lo % 10;
    ascii[(*i_ascii)++] = '0' + lo2 / 10;
    ascii[(*i_ascii)++] = '0' + lo2 % 10;
}

static inline void
advance_itoa_4(
    int integer,
    char *restrict ascii,
    ssize_t *restrict i_ascii
) {
    assert(integer <= 9999);

    int lo = integer % 100;
    int hi = integer / 100;

    ascii[(*i_ascii)++] = '0' + hi / 10;
    ascii[(*i_ascii)++] = '0' + hi % 10;
    ascii[(*i_ascii)++] = '0' + lo / 10;
    ascii[(*i_ascii)++] = '0' + lo % 10;
}

static inline void
advance_itoa_3(
    int integer,
    char *restrict ascii,
    ssize_t *restrict i_ascii
) {
    assert(integer <= 999);

    int lo = integer % 100;
    int hi = integer / 100;

    ascii[(*i_ascii)++] = '0' + hi;
    ascii[(*i_ascii)++] = '0' + lo / 10;
    ascii[(*i_ascii)++] = '0' + lo % 10;
}

static inline void
advance_itoa_2(
    int integer,
    char *restrict ascii,
    ssize_t *restrict i_ascii
) {
    assert(integer <= 99);

    int lo = integer % 10;
    int hi = integer / 10;

    ascii[(*i_ascii)++] = '0' + hi;
    ascii[(*i_ascii)++] = '0' + lo;
}

static inline void
ascii_to_char16(
    char     *ascii,
    char16_t *char16,
    ssize_t strlen
) {
    for (ssize_t i = 0; i < strlen; ++i) {
        char16[i] = ascii[i];
    }
}


#endif
