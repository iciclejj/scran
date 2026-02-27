#include <stdbool.h>
#include <assert.h>

#include "init.h"
#include "state.h"
#include "state-util.h"
#include "event-handlers.h"


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
    wl_surface_commit(st_output->surface.wl_surface);

    return true;
}

void
init_premem__selection__destroy(struct scran_output *st_output)
{
    zwlr_layer_surface_v1_destroy(st_output->surface.layer_surface);
    wl_surface_destroy(st_output->surface.wl_surface);
}


// XXX NOTE: This is for basic initialization that does not care about what we
// will render, other than ensuring buffers etc. are properly set up for the
// given output. More specialized init happens in dispatch_surface_event_loop.
// Maybe this should be refactored to be more immediately obvious...
bool
init_postmem__selection(struct scran_output *st_output)
{
    struct scran_output_selectionContext *const selection_ctx = &st_output->selection_ctx;
    struct scran_output_surface * st_surface = &st_output->surface;

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    // TODO: Probably move this into init/surface.c, and call this just
    // init_selection, or remove this function entirely.
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];

        // Shared memory must already be allocated.
        assert(st_buffer->data != NULL);
        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            get_output_width_logical(st_output),
            get_output_height_logical(st_output),
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_PIXEL_STRIDE * st_output->mode.width_px,
            BL_DATA_ACCESS_RW,
            NULL,
            NULL
        );

        bl_context_init_as(&st_buffer->bl_ctx, &st_buffer->bl_img, NULL);
    }

    bl_path_init(&st_surface->bl_path);

    selection_ctx->bl_box_bounds = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = get_output_width_logical(st_output),
        .y1 = get_output_height_logical(st_output),
    };

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


// XXX TODO: Probably move this somewhere other than init/surface.c
void
set_selection_surface_theme(
    struct scran_output *st_output,
    enum surface_theme action
) {
    struct BLRgba32 fill_style;
    enum BLFillRule fill_rule;

    fill_rule = BL_FILL_RULE_EVEN_ODD;

    switch (action) {
    case SURFACE_THEME_DEFAULT:
        fill_style = BLCONTEXT_RGBA32_FILL_STYLE_DEFAULT;
        break;
    case SURFACE_THEME_VIDEO_CAPTURE:
        fill_style = BLCONTEXT_RGBA32_FILL_STYLE_VIDEO_CAPTURE;
        break;
    }

    struct scran_output_surface *st_surface = &st_output->surface;

    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct scran_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];

        bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, fill_style.value);
        bl_context_set_fill_rule(&st_buffer->bl_ctx, fill_rule);
    }

    // XXX HACK: This forces a full redraw inside of capture::frame
    //               TODO: Probably implement a force_redraw flag or similar,
    //               somewhere that capture::frame can easily read it.
    //               (At least once we implement more complicated scenes that
    //               might change the dirty-rect dynamics that we take
    //               advantage of here.)
    st_surface->bl_box_currently_drawn = st_output->selection_ctx.bl_box_bounds;
}


void
dispatch_selection_surface_event_loop(struct scran_output *st_output)
{
    // TODO: Assert bl_ctx has already begun, or maybe just move its (entire?)
    // init into here.
    struct BLPathCore *bl_path = &st_output->surface.bl_path;

    set_selection_surface_theme(st_output, SURFACE_THEME_DEFAULT);

    struct scran_output_surface_buffer *const initial_buffer = &st_output->surface.double_buffer[0];
    struct BLBoxI *selection_box_bounds = &st_output->selection_ctx.bl_box_bounds;

    // TODO: Verify whether we acutally need to draw the "dispatch"-commit to
    // not get a frame of startup delay.
    bl_path_add_box_i(bl_path, selection_box_bounds, BL_GEOMETRY_DIRECTION_NONE);
    // XXX: At the moment, this function is only used at the start of the
    // program. Handle busy buffers later if/when it will be necessary.
    assert(initial_buffer->busy == false);
    bl_context_fill_path_d(&initial_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, bl_path);
    bl_path_reset(bl_path);

    initial_buffer->busy = true;
    wl_surface_attach(st_output->surface.wl_surface, initial_buffer->wl_buffer, 0, 0);
    wl_surface_commit(st_output->surface.wl_surface);
}

