#ifndef SCRANROT_TEST_TYPES_H
#define SCRANROT_TEST_TYPES_H


#include <stdint.h>

#include "scranrot.h"


struct scranrot_test_dimensions {
    int width_px;
    int height_px;
};

struct scranrot_test_parameters {
    struct scranrot_test_dimensions src_dimensions;
    int                             src_padding_bytes;
    enum scranrot_transform         transform;
    uint32_t                        rgba_shuffle;
};

struct scranrot_test_yuv420_planes {
    uint8_t *y;
    int      y_stride;
    uint8_t *u;
    int      u_stride;
    uint8_t *v;
    int      v_stride;
};


#endif
