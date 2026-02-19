#ifndef INIT_H
#define INIT_H

#include <wayland-client.h>

#include "state.h"


// ARGB8888 and XRGB8888 are always supported (wayland spec)
#define SURFACE_SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SURFACE_SHM_FORMAT_BL BL_FORMAT_PRGB32
#define SURFACE_PIXEL_STRIDE 4 // Bytes per pixel. Depends on SURFACE_SHM_FORMAT.
#define SURFACE_BLCONTEXT_ORIGIN ((BLPoint){0,0})

#define SSE_ALIGNMENT_BYTES 16
#define FRAMEBUFFER_ALIGNMENT_BYTES SSE_ALIGNMENT_BYTES
#define FRAMEBUFFER_PADDING_BYTES SSE_ALIGNMENT_BYTES
#define FRAMEBUFFER_PADDING_PIXELS(pixel_stride) ((FRAMEBUFFER_PADDING_BYTES / pixel_stride) + (FRAMEBUFFER_PADDING_BYTES % pixel_stride != 0))

static inline int32_t
get_surface_stride(struct scran_output_mode *mode) {
    return mode->width_px * SURFACE_PIXEL_STRIDE;
}

static inline int32_t
_get_framebuffer_size_padded(struct scran_output_mode *mode, uint8_t pixel_stride) {
    const int32_t px_padding = FRAMEBUFFER_PADDING_PIXELS(pixel_stride);
    return pixel_stride * (mode->width_px + px_padding) * (mode->height_px + px_padding);
}

static inline int32_t
get_surface_buf_size_padded(struct scran_output_mode *mode) {
    return _get_framebuffer_size_padded(mode, SURFACE_PIXEL_STRIDE);
}

static inline int32_t
get_capture_buf_size_padded(struct scran_output *st_output) {
    return _get_framebuffer_size_padded(&st_output->mode, st_output->capture.frame_ctx.pixel_stride);
}

// These will probably always stay equivalent, but dedicated function avoids
// any second-guessing.
static inline int32_t
get_capture_buf_2_size_padded(struct scran_output *st_output) {
    return get_capture_buf_size_padded(st_output);
}

static inline int32_t
get_capture_stride(struct scran_output *st_output) {
    return st_output->capture.frame_ctx.pixel_stride * st_output->mode.width_px;
}


bool init_output_surface_shm_buffers(struct scran_output *st_output, struct wl_shm *wl_shm_global);

bool init_premem__surface(struct scran_output *st_output, struct scran_globals *st_globals);
 void init_premem__surface__destroy(struct scran_output *st_output);
void dispatch_surface_event_loop(struct scran_output *st_output);
enum surface_theme {
    SURFACE_THEME_DEFAULT,
    SURFACE_THEME_VIDEO_CAPTURE,
};
void set_surface_theme(struct scran_output *st_output, enum surface_theme action);

bool init_premem__capture(struct scran_output *st_output, struct scran_seat_datacontrol *st_datacontrol, struct scran_globals *globals);
void init_premem__capture__destroy(struct scran_output *st_output);

bool init_selection_and_blend2d(struct scran_output *st_output);
 void destroy_selection_and_blend2d(struct scran_output *st_output);

bool init_postwl__capture(struct scran_output *st_output);

int shm_open_anon(void);

#endif
