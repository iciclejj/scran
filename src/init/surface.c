#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "state.h"
#include "event-handlers.h"
#include "init.h"

bool
init_output_surface(
    struct scran_output *st_output,
    struct scran_globals *st_globals
) {
    // Must add role to surface and ack its configure event before adding a buffer.
    st_output->surface.surface = wl_compositor_create_surface(st_globals->compositor);
    st_output->surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        st_globals->layer_shell,
        st_output->surface.surface,
        st_output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "scran-capture" // TODO: Figure out a namespace name?
    );

    zwlr_layer_surface_v1_set_exclusive_zone(st_output->surface.layer_surface, -1);
    // Need to set at least anchors before configure event,
    // so that the compositor knows what width/height to give us.
    zwlr_layer_surface_v1_set_anchor(
        st_output->surface.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
    );
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_output->surface.layer_surface,
        // TODO: Figure out whether this should rather be set to "exclusive"
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
    );

    zwlr_layer_surface_v1_add_listener(st_output->surface.layer_surface, &layer_surface_listener, st_output);
    // Initial bufferless commit to trigger configure event
    wl_surface_commit(st_output->surface.surface);

    return true;
}

void
dispatch_surface_event_loop(struct scran_output *st_output)
{
    struct scran_output_surface *const st_surface = &st_output->surface;
    struct scran_output_surface_buffer *const initial_buffer = &st_surface->double_buffer[0];
    struct scran_output_selection_blend2d *const bl = &st_output->selection.bl;
    const struct BLPoint origin = { 0, 0 };

    bl_context_begin(&bl->ctx, &initial_buffer->bl_img, NULL);

    bl_path_add_box_i(&bl->path, &bl->box_outer, BL_GEOMETRY_DIRECTION_NONE);

    // TODO: Use macros for colors
    bl_context_set_fill_style_rgba32(&bl->ctx, 0x88888888);
    bl_context_fill_path_d(&bl->ctx, &origin, &bl->path);

    bl_context_end(&bl->ctx);
    bl_context_reset(&bl->ctx);

    wl_surface_commit(st_output->surface.surface);
}

void
destroy_output_surface(struct scran_output *st_output)
{
    zwlr_layer_surface_v1_destroy(st_output->surface.layer_surface);
    wl_surface_destroy(st_output->surface.surface);
}

