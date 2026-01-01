#include <wayland-client.h>

#include "state.h"
#include "wayland-event-handlers.h"

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
    struct client_state *state = data;

    state->output.x_global = x_global;
    state->output.y_global = y_global;
    state->output.subpixel_layout = subpixel_layout;
    state->output.transform = transform;

    // TODO: h_phys_mm, y_phys_mm ? For ruler or something?
}


static void
handle_output_scale(
    void *data,
    struct wl_output *output,
    int32_t scale_factor
) {
    struct client_state *state = data;
    state->output.scale = scale_factor;
}

static void
handle_output_mode(
    void *data,
    struct wl_output *output,
    uint32_t flags,
    int32_t width,
    int32_t height,
    int32_t refresh_rate_mhz
) {
    struct client_state *state = data;

    // Non-current modes are deprecated
    if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
        return;
    }

    state->output.mode.width = width;
    state->output.mode.height = height;
    state->output.mode.refresh_rate_mhz = refresh_rate_mhz;
}

// TODO: How to handle done event properly/efficiently?
//           version >= 2: geometry event followed by done event
struct wl_output_listener output_listener = {
    .geometry = handle_output_geometry,
    .scale = handle_output_scale,
    .mode = handle_output_mode,
    .done = noop,
    .name = noop,
    .description = noop,
};
