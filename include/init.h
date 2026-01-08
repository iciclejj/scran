#ifndef INIT_H
#define INIT_H

#include <wayland-client.h>

#include "state.h"


// ARGB8888 and XRGB8888 are always supported (wayland spec)
#define SURFACE_SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SURFACE_SHM_FORMAT_BL BL_FORMAT_PRGB32
#define SURFACE_PIXEL_STRIDE 4 // Bytes per pixel. Depends on SURFACE_SHM_FORMAT.


bool init_surface_shm_buffers(struct client_state_surface *st_surface, struct wl_shm *wl_shm_global);
void destroy_surface_shm_buffers(struct client_state_surface *st_surface);
bool init_surface(struct client_state *state);

int shm_open_anon(void);

#endif
