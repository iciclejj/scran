#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "viewporter.h"
#include "fractional-scale-v1.h"

#include "init.h"
#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "selection.h"
#include "surface.h"


bool
init_premem__selection(
    struct scran_output *st_output,
    struct scran_globals *st_globals
) {
    // Must add role to surface and ack its configure event before adding a buffer.
    st_output->surface.wl_surface = wl_compositor_create_surface(st_globals->compositor);
    st_output->surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        st_globals->layer_shell,
        st_output->surface.wl_surface,
        st_output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "scran-selection"
    );

    // Need to set at least anchors before configure event,
    // so that the compositor knows what width/height to give us.
    zwlr_layer_surface_v1_set_anchor(
        st_output->surface.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
    );
    zwlr_layer_surface_v1_set_exclusive_zone(
        st_output->surface.layer_surface,
        -1
    );
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_output->surface.layer_surface,
        SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
    );

    zwlr_layer_surface_v1_add_listener(st_output->surface.layer_surface, &layer_surface_listener, &st_output->surface);
    // Initial bufferless commit to trigger configure event
    wl_surface_commit(st_output->surface.wl_surface);

    // We call set_destination etc. in the ::scale handlers (and in postmem init)
    st_output->surface.viewport = wp_viewporter_get_viewport(g_state.globals.viewporter, st_output->surface.wl_surface);
    // Leave st_surface->fractional_scale as 0. output->scale is our fallback.
    if (!st_output->scale) { // did not already get set in output::scale
        st_output->scale = 1;
    }
    st_output->surface.fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
        st_globals->fractional_scale_manager, st_output->surface.wl_surface
    );
    wp_fractional_scale_v1_add_listener(st_output->surface.fractional_scale, &fractional_scale_listener, &st_output->surface);

    return true;
}

void
init_premem__selection__destroy(struct scran_output *st_output)
{
    zwlr_layer_surface_v1_destroy(st_output->surface.layer_surface);
    wl_surface_destroy(st_output->surface.wl_surface);
    wp_viewport_destroy(st_output->surface.viewport);
    wp_fractional_scale_v1_destroy(st_output->surface.fractional_scale);
}


// XXX NOTE: This is for basic initialization that does not care about what we
// will render, other than ensuring buffers etc. are properly set up for the
// given output. More specialized init happens in dispatch_surface_event_loop.
// Maybe this should be refactored to be more immediately obvious...
//
// TODO: We should either get a surface init file, or rename this one to
// make it obvious that it is for both surface-specific and general
// selection-related things.
bool
init_postmem__selection(struct scran_output *st_output)
{
    DEBUG("init_postmem__selection()\n");

    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;
    struct scran_output_surface          *st_surface    = &st_output->surface;

    // Sanity check...
    assert(st_output->xdg_geometry.w_logical == st_output->surface.width_logical);
    assert(st_output->xdg_geometry.h_logical == st_output->surface.height_logical);

    // Update here in addition to within the ::scale handlers, since they may
    // have fired before the viewport was initialized.
    update_surface_scale_and_size(st_surface);
    update_surface_viewport(st_surface);

    selection_ctx->box_bounds_px = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        // Capture area bounds.
        .x1 = get_transformed_output_width(st_output),
        .y1 = get_transformed_output_height(st_output),
    };
    // XXX HACK: Get the outline out of view...
    //      A more elegant solution can come when necessary
    selection_ctx->box_px = (struct BLBoxI) {
        .x0 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
        .y0 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
        .x1 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
        .y1 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
    };

    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];

        // Blend2D wrapper for the buffer we will be rendering into
        assert(st_buffer->data != NULL);
        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            st_surface->width_px_buffer,
            st_surface->height_px_buffer,
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_PIXEL_STRIDE * st_surface->width_px_buffer,
            BL_DATA_ACCESS_RW,
            NULL,
            NULL
        );
        bl_context_init_as(&st_buffer->bl_ctx, &st_buffer->bl_img, NULL);
    }

    bl_path_init(&st_surface->bl_path);
    set_selection_surface_theme(st_output, SURFACE_THEME_DEFAULT);


    struct scran_output_surface_buffer *initial_buffer = &st_surface->double_buffer[0];

    // We need to pre-render here if we want the UI to be displayed already on
    // the first frame.
    assert(initial_buffer->busy == false);
    draw_frame_and_damage_buffer(
        &st_output->surface,
        initial_buffer,
        st_output->selection_ctx.box_px,
        st_output->selection_ctx.box_bounds_px
    );
    initial_buffer->busy = true;

    // Dispatch the callback event loop.
    // XXX: For some reason, the initial frame callback is often delayed by
    // significantly more than an entire frametime (e.g. 22ms on 16.67 ms
    // (60fps) frametime). Possibly bound by waiting for layer-surface
    // configure? TODO: Find out if this is compositor-bound or can be
    // worked around somehow.
    wl_surface_attach(
        st_output->surface.wl_surface, initial_buffer->wl_buffer, 0, 0
    );
    wl_callback_add_listener(
        wl_surface_frame(st_output->surface.wl_surface),
        &surface_frame_callback_listener,
        st_output
    );
    wl_surface_commit(
        st_output->surface.wl_surface
    );

    return true;
}

void
init_postmem__selection__destroy(struct scran_output *st_output)
{
    struct scran_output_selectionContext *const selection_ctx = &st_output->selection_ctx;
    struct scran_output_surface * st_surface = &st_output->surface;

    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];

        bl_context_destroy(&st_buffer->bl_ctx);
        bl_image_destroy(&st_buffer->bl_img);
    }

    bl_path_destroy(&st_surface->bl_path);
}

