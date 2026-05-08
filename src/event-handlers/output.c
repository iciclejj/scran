#include <wayland-client.h>

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "print.h"


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
    struct scran_output *st_output = data;

    st_output->transform = transform;

    // TODO: h_phys_mm, y_phys_mm ? For ruler or something?
}


static void
handle_output_scale(
    void *data,
    struct wl_output *wl_output,
    int32_t factor
) {
    struct scran_output *st_output = data;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    DEBUG("output::scale():\n");
    DEBUG("  %d\n", st_output->scale);

    if (st_output->scale != factor) {
        st_output->scale = factor;

        update_surface_scale_bufsize_viewport(st_output);
        reinit_scran_ui(&selection_surface->ui_ctx, selection_surface->surface.final_scale_factor_normalized);
        request_selection_surface_frame_callback(st_output);
    }
}


static void
handle_output_mode(
    void *data,
    struct wl_output *output,
    uint32_t flags,
    // No transform or scale is pre-applied to these
    int32_t width,
    int32_t height,
    int32_t refresh_rate_mHz // millihertz
) {
    struct scran_output *st_output = data;

    DEBUG("output::mode()\n");

    // Non-current modes are deprecated
    if (!(flags & WL_OUTPUT_MODE_CURRENT)) {
        return;
    }

    st_output->mode.width_px = width;
    st_output->mode.height_px = height;
    st_output->mode.refresh_rate_mHz = refresh_rate_mHz;

    DEBUG("  st_output->mode.width_px: %d\n", width);
    DEBUG("  st_output->mode.height_px: %d\n", height);
}


static void
handle_output_done(
    void *data,
    struct wl_output *wl_output
) {
    // TODO ?
}

static void
handle_output_name(
    void *data,
    struct wl_output *wl_output,
    const char *name
) {
    struct scran_output *st_output = data;

    DEBUG("output::name()\n");

    // Store the name so we can match it later with a zwlr_output_head
    //     See 'event-handlers/wlr-output.c'.
    size_t name_strlen = strlcpy(st_output->name, name, sizeof(st_output->name));

    if (name_strlen >= sizeof(st_output->name)) {
        eprintf("Error: wl_output name too long (max %zu): %s\n", sizeof(st_output->name), name);
        exit(EXIT_FAILURE); // XXX TODO: Fail more graciously here?
    }
}

static void
handle_output_description(
    void *data,
    struct wl_output *wl_output,
    const char *description
) {
    // Probably not needed...
}


// TODO: How to handle done event properly/efficiently?
//           version >= 2: geometry event is followed by done event
struct wl_output_listener output_listener = {
    // xdg_output is preferred for most of this.
    .geometry = handle_output_geometry,
    .scale = handle_output_scale,
    .mode = handle_output_mode,
    .done = handle_output_done,
    .name = handle_output_name,
    .description = handle_output_description,
};

