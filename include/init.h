#ifndef SCRAN_INIT_H
#define SCRAN_INIT_H

#include <assert.h>

#include <wayland-client.h>

#include "state.h"


// ARGB8888 and XRGB8888 are always supported (wayland spec)
#define SURFACE_SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SURFACE_SHM_FORMAT_BL BL_FORMAT_PRGB32
#define SURFACE_PIXEL_STRIDE 4 // Bytes per pixel. Depends on SURFACE_SHM_FORMAT.
#define SURFACE_BLCONTEXT_ORIGIN ((BLPoint){0,0})

#define FRAMEBUFFER_ALIGNMENT_BYTES 64 // 64 should cover most bases (cache, simd)

static inline size_t
get_units_until_alignment(
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
get_framebuffer_size(int32_t width_px, int32_t height_px, uint8_t pixel_stride) {
    size_t width_bytes = pixel_stride * width_px;
    return width_bytes * height_px;
}

static inline size_t
get_surface_buf_size_padded(struct scran_output_surface *st_surface) {
    int32_t width_px = st_surface->width_px_buffer;
    int32_t height_px = st_surface->height_px_buffer;

    // XXX HACK(kinda): Ensure that we will always allocate a buffer of at
    // least the largest possibly-required size. For example, some compositors
    // (COSMIC) may end up with a buffer res within (+/-)2 of our native res.
    // This ensures that live scale updates will always be handled smoothly,
    // and never require us to allocate a new, larger buffer.
    //     TODO: Possibly remove this if we will ever support live output-mode
    //     updates, although this would still maybe be worth keeping as a
    //     prophylactic measure.
    //
    // XXX: These asserts only hold if the compositor is trying to achieve
    // native resolution. Some compositors like hyprland at the moment, are
    // instead requesting a multiple of surface-local resolution, and then
    // downscaling
    //     NOTE: This only happens for some of the configure events, not
    //     necessarily the final configure event (which will actually be used).
    // assert(get_transformed_output_width(st_output) - 1 <= width_px && width_px  <= get_transformed_output_width(st_output) + 1);
    // assert(get_transformed_output_height(st_output) - 1 <= height_px && height_px  <= get_transformed_output_height(st_output) + 1);
    width_px += 2;
    height_px += 2;
    return get_framebuffer_size(width_px, height_px, SURFACE_PIXEL_STRIDE);
}

static inline size_t
get_capture_buf_size(struct scran_output *st_output) {
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;
    return get_framebuffer_size(frame_ctx->source_width_px, frame_ctx->source_height_px, frame_ctx->pixel_stride);
}

static inline size_t
get_capture_stride(struct scran_output *st_output) {
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;
    return frame_ctx->pixel_stride * frame_ctx->source_width_px;
}


bool init_output_surface_shm_buffers(struct scran_output *st_output, struct wl_shm *wl_shm_global);

bool init_premem__capture(struct scran_output *st_output, struct scran_seat_datacontrol *st_datacontrol, struct scran_globals *globals);
void init_premem__capture__destroy(struct scran_output *st_output);

bool init_premem__selection(struct scran_output *st_output, struct scran_globals *st_globals);
 void init_premem__selection__destroy(struct scran_output *st_output);
bool init_postmem__selection(struct scran_output *st_output, BLBoxI *custom_initial_selection);
 void init_postmem__selection__destroy(struct scran_output *st_output);

bool init_premem__freezeframe( struct scran_output *st_output);
 void init_premem__freezeframe__destroy( struct scran_output *st_output);

bool init_premem__datacontrol(struct scran_seat_datacontrol *st_datacontrol);
 void init_premem__datacontrol__destroy(struct scran_seat_datacontrol *st_datacontrol);


#endif
