#ifndef INIT_H
#define INIT_H

#include <wayland-client.h>

#include "state.h"


// ARGB8888 and XRGB8888 are always supported (wayland spec)
#define SURFACE_SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SURFACE_SHM_FORMAT_BL BL_FORMAT_PRGB32
#define SURFACE_PIXEL_STRIDE 4 // Bytes per pixel. Depends on SURFACE_SHM_FORMAT.
#define GET_SURFACE_BUF_SIZE(output_mode) (SURFACE_PIXEL_STRIDE * output_mode.width_px * output_mode.height_px)
#define GET_CAPTURE_BUF_SIZE(st_output) (st_output.capture.pixel_stride * st_output.mode.width_px * st_output.mode.height_px)


bool init_output_surface_shm_buffers(struct client_state_output *st_output, struct wl_shm *wl_shm_global);
 void destroy_output_surface_shm_buffers(struct client_state_output_surface *st_surface);

bool init_output_surface(struct client_state_output *st_output, struct client_state_globals *st_globals);

bool init_capture(struct client_state_output *st_output, struct client_state_globals *globals);
bool init_image_copy_capture_shm_buffer(struct client_state_output *st_output, struct client_state_globals *globals);
void destroy_capture_shm_buffers(struct client_state_output_capture *st_capture);

int shm_open_anon(void);

#endif
