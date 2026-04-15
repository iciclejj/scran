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
#include "surface__selection.h"


bool
init_premem__selection(
    struct scran_output *st_output,
    struct scran_globals *st_globals
) {
    struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

    // Must add role to surface and ack its configure event before adding a buffer.
    st_surface->wl_surface = wl_compositor_create_surface(st_globals->compositor);
    st_surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        st_globals->layer_shell,
        st_surface->wl_surface,
        st_output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "scran-selection"
    );

    // Need to set at least anchors before configure event,
    // so that the compositor knows what width/height to give us.
    zwlr_layer_surface_v1_set_anchor(
        st_surface->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
    );
    zwlr_layer_surface_v1_set_exclusive_zone(
        st_surface->layer_surface,
        -1
    );
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_surface->layer_surface,
        SCRAN_LAYER_SURFACE_KEYBOARD_INTERACTIVITY_FOCUSED
    );

    zwlr_layer_surface_v1_add_listener(st_surface->layer_surface, &layer_surface_listener__selection, &st_output->selection_surface);
    // Initial bufferless commit to trigger configure event
    wl_surface_commit(st_surface->wl_surface);

    // We call set_destination etc. in the ::scale handlers (and in postmem init)
    st_surface->viewport = wp_viewporter_get_viewport(g_state.globals.viewporter, st_surface->wl_surface);
    // Leave st_surface->fractional_scale as 0. output->scale is our fallback.
    if (!st_output->scale) { // did not already get set in output::scale
        st_output->scale = 1;
    }
    st_surface->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(
        st_globals->fractional_scale_manager, st_surface->wl_surface
    );
    wp_fractional_scale_v1_add_listener(st_surface->fractional_scale, &fractional_scale_listener__selection, &st_output->selection_surface);

    return true;
}

void
init_premem__selection__destroy(struct scran_output *st_output)
{
    struct scran_output_surface *st_surface = &st_output->selection_surface.surface;

    zwlr_layer_surface_v1_destroy(st_surface->layer_surface);
    wl_surface_destroy(st_surface->wl_surface);
    wp_viewport_destroy(st_surface->viewport);
    wp_fractional_scale_v1_destroy(st_surface->fractional_scale);
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
init_postmem__selection(struct scran_output *st_output, BLBoxI *custom_initial_selection)
{
    DEBUG("init_postmem__selection()\n");

    struct scran_output_selectionContext *selection_ctx     = &st_output->selection_ctx;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct scran_output_surface          *st_surface        = &st_output->selection_surface.surface;


    // Sanity check...
    assert(st_output->xdg_geometry.w_logical == st_surface->width_logical);
    assert(st_output->xdg_geometry.h_logical == st_surface->height_logical);

    // Update here in addition to within the ::scale handlers, since they may
    // have fired before the viewport was initialized.
    update_surface_scale_and_size(st_surface);
    update_surface_viewport(st_surface);
    request_selection_surface_update(st_output);

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &selection_surface->double_buffer[i];

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

    bl_path_init(&selection_surface->bl_path);
    set_selection_surface_theme(st_output, SURFACE_THEME_DEFAULT);

    BLBoxI initial_box;
    if (custom_initial_selection != NULL) {
        initial_box = *custom_initial_selection;
    } else {
        // HACK: Get the outline out of view...
        //       TODO: Make a redraw function that doesn't draw the selection.
        initial_box = (struct BLBoxI) {
            .x0 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
            .y0 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
            .x1 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
            .y1 = 0 - ceil(SCRAN_SELECTION_BORDER_THICKNESS_PX),
        };
    }

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *buffer = &selection_surface->double_buffer[i];
        // We need to pre-render here if we want the UI to be displayed already
        // on the first frame.
        //
        // We draw *both* buffers, to simplify the handling of e.g.
        // buffer->box_already_drawn for custom vs "out of view" initial box,
        // etc.
        //   TODO: Look at this again later and find a clean and robust way to
        //   not have to initialize both frames. Probably after (or during)
        //   merging freezeframe, since selection init will have to be somewhat
        //   more set in stone after that. (Just shoving the non-initial one
        //   out of view (and not intersecting the initial one) or something
        //   like that should technically be enough..?)
        //
        // XXX: For some reason, the initial frame callback is often delayed by
        // significantly more than an entire frametime (e.g. 22ms on 16.67 ms
        // (60fps) frametime). Possibly bound by waiting for layer-surface
        // configure? TODO: Find out if this is compositor-bound or can be
        // worked around somehow.
        assert(buffer->busy == false);
        force_update_selection_surface(st_output, buffer, initial_box);
    }

    return true;
}

void
init_postmem__selection__destroy(struct scran_output *st_output)
{
    struct scran_output_selectionContext *selection_ctx     = &st_output->selection_ctx;
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct scran_output_surface          *st_surface        = &st_output->selection_surface.surface;

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &selection_surface->double_buffer[i];

        bl_context_destroy(&st_buffer->bl_ctx);
        bl_image_destroy(&st_buffer->bl_img);
    }

    bl_path_destroy(&selection_surface->bl_path);
}

