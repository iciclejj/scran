#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "implementations.h"
#include "scranrot.h"

#include "./test-util.h"
#include "./rgba32-to-rgba32/reference.h"
#include "./rgba32-to-yuv420/reference.h"


#define ARRAY_LENGTH(arr)         (sizeof(arr) / sizeof(*arr))

static const uint32_t SCRANROT_TEST_SEED_RGBA32 = 0x12345678u;
static const uint32_t SCRANROT_TEST_SEED_YUV420 = 0x87654321u;

enum {
    // The reference uses exact rounded BT.601/JFIF coefficients; production
    // intentionally uses fast /256 fixed-point approximations.
    SCRANROT_TEST_YUV420_Y_TOLERANCE  = 1,
    SCRANROT_TEST_YUV420_UV_TOLERANCE = 1,
};


static inline uint32_t
generate_test_seed(
    uint32_t suite_seed,
    const struct scranrot_test_parameters *params
) {
    return suite_seed
        ^  (uint32_t)(params->src_dimensions.width_px  * 131u)
        ^  (uint32_t)(params->src_dimensions.height_px * 17u)
        ^ ((uint32_t) params->src_padding_bytes        << 16)
        ^ ((uint32_t) params->transform                << 12)
        ^  (uint32_t)(params->rgba_shuffle             << 8);
}

static inline int
get_src_stride(const struct scranrot_test_parameters *params) {
    return params->src_dimensions.width_px * RGBA32_PIXEL_STRIDE + params->src_padding_bytes;
}

static inline size_t
get_src_size(const struct scranrot_test_parameters *params, size_t src_stride_bytes) {
    return src_stride_bytes * (size_t)params->src_dimensions.height_px;
}

static size_t
get_yuv420_dst_size(const struct scranrot_test_dimensions dimensions) {
    const int y_stride  = reference_y_stride(dimensions);
    const int uv_stride = reference_uv_stride(dimensions);
    return reference_y_size(dimensions, y_stride) + 2 * reference_uv_size(dimensions, uv_stride);
}

static void
print_case_failure(
    const char                            *case_name,
    const char                            *impl_name,
    const struct scranrot_test_parameters *params,
    uint32_t                               seed
) {
    fprintf(
        stderr,
        "%s case failed: impl=%s, %dx%d, padding=%d, shuffle=0x%08x, transform=%s, seed=0x%08x\n",
        case_name,
        impl_name,
        params->src_dimensions.width_px,
        params->src_dimensions.height_px,
        params->src_padding_bytes,
        params->rgba_shuffle,
        scranrot_test_transform_name(params->transform),
        seed
    );
}


static bool
run_rgba32_to_rgba32_case(
    const char                            *impl_name,
    scranrot_transform_framebuffer_fn     *fn,
    const struct scranrot_test_parameters *params
) {
    const int                       src_stride_bytes = get_src_stride(params);
    const size_t                    src_size         = get_src_size(params, src_stride_bytes);
    uint8_t *const                  src              = scranrot_test_xmalloc(src_size);

    const struct scranrot_test_dimensions dst_dimensions = scranrot_test_get_reference_dst_dimensions(params->src_dimensions, params->transform);
    const size_t                    dst_size         = (size_t)dst_dimensions.width_px * (size_t)dst_dimensions.height_px * RGBA32_PIXEL_STRIDE;
    uint8_t *const                  dst              = scranrot_test_xmalloc(dst_size);
    uint8_t *const                  reference_dst    = scranrot_test_xmalloc(dst_size);

    const uint32_t seed = generate_test_seed(SCRANROT_TEST_SEED_RGBA32, params);

    scranrot_test_generate_test_image(
        src, src_size, params->src_dimensions, src_stride_bytes,
        seed
    );
    memset(dst, 0xcd, dst_size);
    memset(reference_dst, 0xcd, dst_size);

    uintptr_t result_dst_stride = 0;
    uintptr_t reference_result_dst_stride = 0;

    bool ok = fn(
        src, params->src_dimensions.width_px, params->src_dimensions.height_px, src_stride_bytes,
        dst, params->rgba_shuffle, params->transform, &result_dst_stride
    );
    bool reference_ok = scranrot_test_reference_rgba32_to_rgba32(
        src, params->src_dimensions.width_px, params->src_dimensions.height_px, src_stride_bytes,
        reference_dst, params->rgba_shuffle, params->transform, &reference_result_dst_stride
    );

    bool pass = true;

    if (ok != reference_ok) {
        fprintf(stderr, "  return mismatch: got %s, reference %s\n",
                ok ? "true" : "false", reference_ok ? "true" : "false"
        );
        pass = false;
    } else if (ok) {
        if (result_dst_stride != reference_result_dst_stride) {
            fprintf(
                stderr, "  stride mismatch: got %zu, reference %zu\n",
                (size_t)result_dst_stride, (size_t)reference_result_dst_stride
            );
            pass = false;
        } else if (!scranrot_test_compare_bytes(dst, reference_dst, dst_size, "RGBA")) {
            pass = false;
        }
    }

    if (!pass) {
        print_case_failure("RGBA", impl_name, params, seed);
    }

    free(src);
    free(dst);
    free(reference_dst);
    return pass;
}

static bool
run_rgba32_to_rgba32_suite(
    const char                        *impl_name,
    scranrot_transform_framebuffer_fn *fn,
    int                               *test_count
) {
    static const struct scranrot_test_dimensions dimensions[] = {
        { 4, 4 },
        { 5, 4 },
        { 4, 5 },
        { 7, 9 },
        { 17, 19 },
    };
    static const enum scranrot_transform transforms[] = {
        SCRANROT_TRANSFORM_NORMAL,
        SCRANROT_TRANSFORM_90,
        SCRANROT_TRANSFORM_180,
        SCRANROT_TRANSFORM_270,
        SCRANROT_TRANSFORM_FLIPPED,
        SCRANROT_TRANSFORM_FLIPPED_90,
        SCRANROT_TRANSFORM_FLIPPED_180,
        SCRANROT_TRANSFORM_FLIPPED_270,
    };
    static const uint32_t shuffles[] = {
        0x03020100u, // identity
        0x00010203u, // reverse channel order
    };
    static const int src_padding_bytes[] = {
        0,
        7,
    };

    for (size_t d = 0; d < ARRAY_LENGTH(dimensions); ++d) {
        for (size_t t = 0; t < ARRAY_LENGTH(transforms); ++t) {
            for (size_t s = 0; s < ARRAY_LENGTH(shuffles); ++s) {
                for (size_t p = 0; p < ARRAY_LENGTH(src_padding_bytes); ++p) {

                    const struct scranrot_test_parameters params = {
                        .src_dimensions    = dimensions[d],
                        .src_padding_bytes = src_padding_bytes[p],
                        .transform         = transforms[t],
                        .rgba_shuffle      = shuffles[s],
                    };

                    if (!run_rgba32_to_rgba32_case(impl_name, fn, &params)) {
                        return false;
                    }

                    ++(*test_count);

                }
            }
        }
    }

    return true;
}


static bool
compare_yuv420_plane(
    const uint8_t *got, int got_stride,
    const uint8_t *reference, int reference_stride,
    int            width,
    int            height,
    const char    *plane_name,
    int            tolerance
) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t got_px       = got      [(size_t)y * (size_t)got_stride       + (size_t)x];
            const uint8_t reference_px = reference[(size_t)y * (size_t)reference_stride + (size_t)x];
            const int     diff         = abs((int)got_px - (int)reference_px);
            if (diff > tolerance) {
                fprintf(
                    stderr, "  %s mismatch at x %d y %d: got %u, reference %u\n",
                    plane_name, x, y, (unsigned)got_px, (unsigned)reference_px
                );
                return false;
            }
        }
    }

    return true;
}

static bool
compare_yuv420(
    const struct scranrot_test_yuv420_planes got,
    const struct scranrot_test_yuv420_planes reference,
    const struct scranrot_test_dimensions    dimensions,
    int                                      y_tolerance,
    int                                      uv_tolerance
) {
    const int y_width   = reference_y_width(dimensions);
    const int y_height  = reference_y_height(dimensions);
    const int uv_width  = reference_uv_width(dimensions);
    const int uv_height = reference_uv_height(dimensions);

    // The production paths use fast approximations of the true BT.601/JFIF
    // reference, so we allow some small tolerances.
    if (!compare_yuv420_plane(
            got.y, got.y_stride, reference.y, reference.y_stride,
            y_width, y_height, "YUV420 Y", y_tolerance)
    ) {
        return false;
    }
    if (!compare_yuv420_plane(
            got.u, got.u_stride, reference.u, reference.u_stride,
            uv_width, uv_height, "YUV420 U", uv_tolerance)
    ) {
        return false;
    }
    if (!compare_yuv420_plane(
            got.v, got.v_stride, reference.v, reference.v_stride,
            uv_width, uv_height, "YUV420 V", uv_tolerance)
    ) {
        return false;
    }

    return true;
}

static bool
run_rgba32_to_yuv420_case(
    const char                               *impl_name,
    scranrot_transform_framebuffer_to_yuv_fn *fn,
    const struct scranrot_test_parameters    *params,
    int                                       y_tolerance,
    int                                       uv_tolerance
) {
    const int                       src_stride_bytes = get_src_stride(params);
    const size_t                    src_size         = get_src_size(params, src_stride_bytes);
    uint8_t *const                  src              = scranrot_test_xmalloc(src_size);

    const struct scranrot_test_dimensions dst_dimensions = scranrot_test_get_reference_dst_dimensions(params->src_dimensions, params->transform);
    const size_t                    dst_size         = get_yuv420_dst_size(dst_dimensions);
    uint8_t *const                  dst              = scranrot_test_xmalloc(dst_size);
    uint8_t *const                  reference_dst    = scranrot_test_xmalloc(dst_size);

    const uint32_t seed = generate_test_seed(SCRANROT_TEST_SEED_YUV420, params);

    scranrot_test_generate_test_image(
        src, src_size, params->src_dimensions, src_stride_bytes,
        seed
    );
    memset(dst, 0xcd, dst_size);
    memset(reference_dst, 0xcd, dst_size);

    struct scranrot_test_yuv420_planes result_planes    = {0};
    struct scranrot_test_yuv420_planes reference_planes = {0};

    bool ok = fn(
        src, params->src_dimensions.width_px, params->src_dimensions.height_px, src_stride_bytes,
        dst, params->rgba_shuffle, params->transform,
        &result_planes.y, &result_planes.y_stride,
        &result_planes.u, &result_planes.u_stride,
        &result_planes.v, &result_planes.v_stride
    );

    bool reference_ok = scranrot_test_reference_rgba32_to_yuv420(
        src, params->src_dimensions.width_px, params->src_dimensions.height_px, src_stride_bytes,
        reference_dst, params->rgba_shuffle, params->transform,
        &reference_planes
    );

    bool pass = true;

    if (ok != reference_ok) {
        fprintf(stderr, "  return mismatch: got %s, reference %s\n", ok ? "true" : "false", reference_ok ? "true" : "false");
        pass = false;
    } else if (ok) {
        if (!compare_yuv420(
                result_planes, reference_planes,
                dst_dimensions, y_tolerance, uv_tolerance)
        ) {
            pass = false;
        }
    }

    if (!pass) {
        print_case_failure("YUV420", impl_name, params, seed);
    }

    free(src);
    free(dst);
    free(reference_dst);
    return pass;
}

static bool
run_rgba32_to_yuv420_suite(
    const char                               *impl_name,
    scranrot_transform_framebuffer_to_yuv_fn *fn,
    int                                       y_tolerance,
    int                                       uv_tolerance,
    int                                      *test_count
) {
    static const struct scranrot_test_dimensions dimensions[] = {
        { 2, 2 },
        { 4, 4 },
        { 6, 8 },
        { 34, 34 },
        { 66, 70 },
    };
    static const enum scranrot_transform transforms[] = {
        SCRANROT_TRANSFORM_NORMAL,
        SCRANROT_TRANSFORM_90,
        SCRANROT_TRANSFORM_180,
        SCRANROT_TRANSFORM_270,
        SCRANROT_TRANSFORM_FLIPPED,
        SCRANROT_TRANSFORM_FLIPPED_90,
        SCRANROT_TRANSFORM_FLIPPED_180,
        SCRANROT_TRANSFORM_FLIPPED_270,
    };
    static const uint32_t shuffles[] = {
        0x03020100u,
        0x00010203u,
    };
    static const int src_padding_bytes[] = {
        0,
        7,
    };

    for (size_t d = 0; d < ARRAY_LENGTH(dimensions); ++d) {
        for (size_t t = 0; t < ARRAY_LENGTH(transforms); ++t) {
            for (size_t s = 0; s < ARRAY_LENGTH(shuffles); ++s) {
                for (size_t p = 0; p < ARRAY_LENGTH(src_padding_bytes); ++p) {

                    const struct scranrot_test_parameters params = {
                        .src_dimensions    = dimensions[d],
                        .src_padding_bytes = src_padding_bytes[p],
                        .transform         = transforms[t],
                        .rgba_shuffle      = shuffles[s],
                    };

                    if (!run_rgba32_to_yuv420_case(impl_name, fn, &params, y_tolerance, uv_tolerance)) {
                        return false;
                    }

                    ++(*test_count);

                }
            }
        }
    }

    return true;
}


static bool
run_yuv420_public_odd_size_case(
    int                                    *test_count,
    const struct scranrot_test_dimensions  src_dimensions
) {
    const struct scranrot_test_parameters params = {
        .src_dimensions    = src_dimensions,
        .src_padding_bytes = 0,
        .transform         = SCRANROT_TRANSFORM_NORMAL,
        .rgba_shuffle      = 0x03020100u,
    };

    // Public YUV420 rejection should happen before dst is used, but we allocate
    // a safely even-sized buffer to keep this test focused on the return value.
    const struct scranrot_test_dimensions dst_dimensions = {
        // Round up to nearest even number
        .width_px  = src_dimensions.width_px  + (src_dimensions.width_px  & 1),
        .height_px = src_dimensions.height_px + (src_dimensions.height_px & 1),
    };

    const int      src_stride_bytes = get_src_stride(&params);
    const size_t   src_size         = get_src_size(&params, src_stride_bytes);
    uint8_t *const src              = scranrot_test_xmalloc(src_size);

    const size_t   dst_size         = get_yuv420_dst_size(dst_dimensions);
    uint8_t *const dst              = scranrot_test_xmalloc(dst_size);

    const uint32_t seed             = generate_test_seed(SCRANROT_TEST_SEED_YUV420, &params);

    scranrot_test_generate_test_image(
        src, src_size, params.src_dimensions, src_stride_bytes,
        seed
    );

    struct scranrot_test_yuv420_planes result_planes = {0};

    const bool ok = scranrot_transform_framebuffer_to_yuv420(
        src, params.src_dimensions.width_px, params.src_dimensions.height_px,
        src_stride_bytes, dst, params.rgba_shuffle, params.transform,
        &result_planes.y, &result_planes.y_stride,
        &result_planes.u, &result_planes.u_stride,
        &result_planes.v, &result_planes.v_stride
    );

    free(src);
    free(dst);

    ++(*test_count);
    if (ok) {
        fprintf(stderr, "YUV420 public odd-size rejection test failed\n");
        print_case_failure("YUV420", "yuv420-public", &params, seed);
        return false;
    }

    return true;
}

static bool
run_yuv420_public_odd_size_suite(int *test_count)
{
    static const struct scranrot_test_dimensions dimensions[] = {
        { 3, 4 },
        { 4, 3 },
        { 3, 3 },
    };

    for (size_t d = 0; d < ARRAY_LENGTH(dimensions); ++d) {
        if (!run_yuv420_public_odd_size_case(test_count, dimensions[d])) {
            return false;
        }
    }

    return true;
}


int main(void)
{
    int test_count = 0;

    scranrot_init();

    if (!run_rgba32_to_rgba32_suite("rgba-fallback", scranrot_transform_framebuffer_fallback, &test_count)) {
        return EXIT_FAILURE;
    }
    if (!run_rgba32_to_rgba32_suite("rgba-public", scranrot_transform_framebuffer, &test_count)) {
        return EXIT_FAILURE;
    }
#if defined(__x86_64__) || defined(__i386__)
    if (scranrot_test_cpu_has_ssse3()) {
        if (!run_rgba32_to_rgba32_suite("rgba-ssse3", scranrot_transform_framebuffer_ssse3, &test_count)) {
            return EXIT_FAILURE;
        }
    }
#endif

    if (!run_rgba32_to_yuv420_suite(
            "yuv420-fallback", scranrot_transform_framebuffer_to_yuv420_fallback,
            SCRANROT_TEST_YUV420_Y_TOLERANCE, SCRANROT_TEST_YUV420_UV_TOLERANCE,
            &test_count)
    ) {
        return EXIT_FAILURE;
    }
    if (!run_rgba32_to_yuv420_suite(
            "yuv420-public", scranrot_transform_framebuffer_to_yuv420,
            SCRANROT_TEST_YUV420_Y_TOLERANCE, SCRANROT_TEST_YUV420_UV_TOLERANCE,
            &test_count)
    ) {
        return EXIT_FAILURE;
    }
#if defined(__x86_64__) || defined(__i386__)
    if (scranrot_test_cpu_has_ssse3()) {
        if (!run_rgba32_to_yuv420_suite(
                "yuv420-ssse3", scranrot_transform_framebuffer_to_yuv420_ssse3,
                SCRANROT_TEST_YUV420_Y_TOLERANCE, SCRANROT_TEST_YUV420_UV_TOLERANCE,
                &test_count)
        ) {
            return EXIT_FAILURE;
        }
    }
    if (scranrot_test_cpu_has_avx2()) {
        if (!run_rgba32_to_yuv420_suite(
                "yuv420-avx2", scranrot_transform_framebuffer_to_yuv420_avx2,
                SCRANROT_TEST_YUV420_Y_TOLERANCE, SCRANROT_TEST_YUV420_UV_TOLERANCE,
                &test_count)
        ) {
            return EXIT_FAILURE;
        }
    }
#endif

    if (!run_yuv420_public_odd_size_suite(&test_count)) {
        return EXIT_FAILURE;
    }

    fprintf(stderr, "scranrot tests passed (%d cases).\n", test_count);
    return EXIT_SUCCESS;
}
