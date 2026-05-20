#ifndef SCRANROT_FALLBACK_H
#define SCRANROT_FALLBACK_H


#include "../include/scranrot.h"


static inline bool
scranrot_fallback_dimensions_supported(int src_width_px, int src_height_px) {
    return src_width_px >= SCRANROT_FALLBACK_STRIDE_PX && src_height_px >= SCRANROT_FALLBACK_STRIDE_PX;
}


#endif
