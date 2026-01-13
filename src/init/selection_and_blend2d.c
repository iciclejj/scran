#include <stdbool.h>
#include <assert.h>

#include "init.h"
#include "state.h"

bool
init_selection_and_blend2d(struct client_state_output *st_output)
{
    struct client_state_output_selection_blend2d *bl = &st_output->selection.bl;
    struct client_state_output_surface * st_surface = &st_output->surface;

    bl_context_init(&bl->ctx);
    bl_path_init(&bl->path);

    // XXX: Maybe handle this assert more robustly
    assert(st_output->mode.width_px != 0);
    bl->box_outer = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = st_output->mode.width_px,
        .y1 = st_output->mode.height_px,
    };

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct client_state_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];
        // Shared memory must already be allocated.
        assert(st_buffer->data != NULL);

        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            st_output->mode.width_px,
            st_output->mode.height_px,
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_PIXEL_STRIDE * st_output->mode.width_px,
            BL_DATA_ACCESS_RW,
            NULL,
            NULL
        );
    }

    return true;
}
