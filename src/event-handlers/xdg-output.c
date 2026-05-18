#include <wayland-client.h>

#include "xdg-output-unstable-v1.h"

#include "state.h"


static void
handle_xdg_output_logical_position(
    void *data,
    struct zxdg_output_v1 *xdg_output,
    int x_px,
    int y_px
) {
    struct scran_output *st_output = data;

    st_output->xdg_geometry.x_logical = x_px;
    st_output->xdg_geometry.y_logical = y_px;
}


static void
handle_xdg_output_logical_size(
    void *data,
    struct zxdg_output_v1 *xdg_output,
    int width_px,
    int height_px
) {
    struct scran_output *st_output = data;

    st_output->xdg_geometry.w_logical = width_px;
    st_output->xdg_geometry.h_logical = height_px;
}


static void
handle_xdg_output_done(
    void *data,
    struct zxdg_output_v1 *xdg_output
) {
    // TODO: Destroy xdg output here instead?
}


struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = handle_xdg_output_logical_position,
    .logical_size = handle_xdg_output_logical_size,
    .done = handle_xdg_output_done,
};
