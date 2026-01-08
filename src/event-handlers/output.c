#include <wayland-client.h>

#include "state.h"
#include "wayland-event-handlers.h"

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
    .geometry = noop,
    .scale = noop,
    .mode = handle_output_mode,
    .done = noop,
    .name = noop,
    .description = noop,
};
