#include <stdbool.h>
#include <assert.h>

#include "init.h"
#include "state.h"

// XXX NOTE: This is for basic initialization that does not care about what we
// will render, other than ensuring buffers etc. are properly set up for the
// given output. More specialized init happens in dispatch_surface_event_loop.
// Maybe this should be refactored to be more immediately obvious...
bool
init_selection_and_blend2d(struct scran_output *st_output)
{
    struct scran_output_selectionContext *const st_selection = &st_output->selection;
    struct scran_output_surface * st_surface = &st_output->surface;

    bl_context_init(&st_selection->bl_ctx);
    bl_path_init(&st_selection->bl_path);

    st_selection->bl_box_outer = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = st_output->mode.width_px,
        .y1 = st_output->mode.height_px,
    };

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];
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

void
destroy_selection_and_blend2d(struct scran_output *st_output)
{
    struct scran_output_selectionContext *const st_selection = &st_output->selection;
    struct scran_output_surface * st_surface = &st_output->surface;

    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];

        bl_image_destroy(&st_buffer->bl_img);
    }

    bl_context_destroy(&st_selection->bl_ctx);
    bl_path_destroy(&st_selection->bl_path);
}

