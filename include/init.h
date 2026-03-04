#ifndef INIT_H
#define INIT_H

#include <wayland-client.h>

#include "state.h"
#include "simd.h"


// ARGB8888 and XRGB8888 are always supported (wayland spec)
#define SURFACE_SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SURFACE_SHM_FORMAT_BL BL_FORMAT_PRGB32
#define SURFACE_PIXEL_STRIDE 4 // Bytes per pixel. Depends on SURFACE_SHM_FORMAT.
#define SURFACE_BLCONTEXT_ORIGIN ((BLPoint){0,0})

#define SSE_ALIGNMENT_BYTES 16
#define FRAMEBUFFER_ALIGNMENT_BYTES 64 // 64 should cover most bases (cache, simd)
#define FRAMEBUFFER_RIGHT_ALIGNMENT_BYTES  SSE_ALIGNMENT_BYTES
#define FRAMEBUFFER_BOTTOM_ALIGNMENT_PX    SSE_ROW_STRIDE


static inline size_t
get_required_padding(
    size_t size_or_offset,
    size_t alignment
) {
    size_t units_past_alignment = alignment == 0 ? 0 : size_or_offset % alignment;
    size_t units_to_next_alignment = alignment - units_past_alignment;

    return units_past_alignment == 0 ? 0 : units_to_next_alignment;
}

static inline size_t
get_surface_stride(struct scran_output_mode *mode) {
    return mode->width_px * SURFACE_PIXEL_STRIDE;
}

static inline size_t
_get_framebuffer_size_padded(struct scran_output_mode *mode, uint8_t pixel_stride) {
    size_t width_bytes = pixel_stride * mode->width_px;
    size_t right_padding_bytes = get_required_padding(width_bytes, FRAMEBUFFER_RIGHT_ALIGNMENT_BYTES);
    size_t bottom_padding_pixels = get_required_padding(mode->height_px, FRAMEBUFFER_BOTTOM_ALIGNMENT_PX);

    return   (width_bytes + right_padding_bytes)
           * (mode->height_px + bottom_padding_pixels);
}

static inline size_t
get_surface_buf_size_padded(struct scran_output_mode *mode) {
    return _get_framebuffer_size_padded(mode, SURFACE_PIXEL_STRIDE);
}

static inline size_t
get_capture_buf_size_padded(struct scran_output *st_output) {
    return _get_framebuffer_size_padded(&st_output->mode, st_output->capture.frame_ctx.pixel_stride);
}

// These will probably always stay equivalent, but dedicated function avoids
// any second-guessing.
static inline size_t
get_capture_buf_2_size_padded(struct scran_output *st_output) {
    return get_capture_buf_size_padded(st_output);
}

static inline size_t
get_capture_stride(struct scran_output *st_output) {
    return st_output->capture.frame_ctx.pixel_stride * st_output->mode.width_px;
}


bool init_output_surface_shm_buffers(struct scran_output *st_output, struct wl_shm *wl_shm_global);

bool init_premem__capture(struct scran_output *st_output, struct scran_seat_datacontrol *st_datacontrol, struct scran_globals *globals);
void init_premem__capture__destroy(struct scran_output *st_output);

bool init_premem__selection(struct scran_output *st_output, struct scran_globals *st_globals);
 void init_premem__selection__destroy(struct scran_output *st_output);
bool init_postmem__selection(struct scran_output *st_output);
 void init_postmem__selection__destroy(struct scran_output *st_output);
void dispatch_selection_surface_event_loop(struct scran_output *st_output);

#endif
