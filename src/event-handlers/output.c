#include <wayland-client.h>

#include "state.h"
#include "event-handlers.h"


static void
handle_output_geometry(
    void *data,
    struct wl_output *output,
    int32_t x_global,
    int32_t y_global,
    int32_t h_phys_mm,
    int32_t w_phys_mm,
    int32_t subpixel_layout,
    const char *make,
    const char *model,
    int32_t transform
) {
    struct client_state_output *st_output = data;

    st_output->transform = transform;

    // TODO: h_phys_mm, y_phys_mm ? For ruler or something?
}

// TODO:
//     Figure out some simple but robust set of asserts or conditions that
//     ensures [ output_mode(/xdg), capture_source, layer_shell/surface,
//     anything_else? ] all have properly synced height/width(/other?)
//      OR some system that treats f.ex. xdg_output as the ground truth somehow.
//     NOTE: f.ex. output::mode and capture_session::buffer_size args seem to
//           ignore applied transforms. Certain others are pre-transformed.
//
static void
handle_output_mode(
    void *data,
    struct wl_output *output,
    uint32_t flags,
    int32_t width,
    int32_t height,
    int32_t refresh_rate_mhz
) {
    struct client_state_output *st_output = data;

    // Non-current modes are deprecated
    if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
        return;
    }

    st_output->mode.width_px = width;
    st_output->mode.height_px = height;
    st_output->mode.refresh_rate_mhz = refresh_rate_mhz;
}

// TODO: How to handle done event properly/efficiently?
//           version >= 2: geometry event followed by done event
struct wl_output_listener output_listener = {
    // xdg_output is preferred for most of this.
    .geometry = handle_output_geometry,
    .scale = noop,
    .mode = handle_output_mode,
    .done = noop,
    .name = noop,
    .description = noop,
};
