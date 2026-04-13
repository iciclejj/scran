#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "presentation-time.h"

#include "state-util.h"
#include "selection.h"
#include "state.h"
#include "event-handlers.h"
#include "util/blend2d.h"
#include "surface.h"
#include "print.h"


extern struct scran g_state;


// TODO: Move into blend2d utils header
static inline bool
_boxes_are_equal(BLBoxI a, BLBoxI b)
{
    return  a.x0 == b.x0 &&
            a.x1 == b.x1 &&
            a.y0 == b.y0 &&
            a.y1 == b.y1
    ;
}

static inline struct scran_output_surface_buffer *
get_free_double_buffer(struct scran_output *st_output)
{
    struct scran_output_surface_buffer *buffer =
        st_output->surface.double_buffer[0].busy
        ? &st_output->surface.double_buffer[1]
        : &st_output->surface.double_buffer[0]
    ;

    if (buffer->busy) {
        return NULL;
    }

    return buffer;
}


static void
surface_frame_callback_handler(
    void *data,
    struct wl_callback *callback,
    uint32_t time_ms
) {
    // Destroy callback here and request new frame "recursively" within callback
    wl_callback_destroy(callback);

    struct scran_output *st_output = data;

    if (!st_output->dirty) {
        goto done;
    }

    struct scran_output_surface_buffer *st_buffer = get_free_double_buffer(st_output);

    if (st_buffer == NULL) {
        DEBUG("Both buffers busy...\n");
        request_selection_surface_frame_callback(st_output);
        return;
    }

    const struct BLBoxI capture_area = get_blboxi_deinverted(st_output->selection_ctx.box_px);
    assert(capture_area.x1 <= get_transformed_output_width(st_output));
    assert(capture_area.y1 <= get_transformed_output_height(st_output));
    const struct BLBoxI capture_area_previous_surface_commit = st_output->surface.box_last_drawn;

    if (!st_buffer->force_redraw && _boxes_are_equal(capture_area, capture_area_previous_surface_commit)) {
        goto done;
    }

    st_buffer->busy = true;

    // XXX HACK: Temporary (hopefully) workaround for regression introduced when
    // trying to fix cosmic sync by assigning on presentation_feedback::presented.
    // Should be removed once the syncing logic is more robust, but will not be
    // easy.
    if (g_state.globals.cosmic_output_manager == NULL) {
        // XXX TODO: Check whether we're actually sway more robustly, and assign
        // it as part of our state. (So we don't need to assume the user is
        // running either cosmic or sway.)
        st_output->capture.frame_ctx.capture_area_px = get_reverse_transform(
            st_output->selection_ctx.box_px,
            st_output->mode.width_px,
            st_output->mode.height_px,
            st_output->transform
        );
    }

    draw_frame_and_damage_buffer(
        &st_output->surface,
        st_buffer,
        capture_area,
        st_output->selection_ctx.box_bounds_px
    );
    st_output->surface.box_last_drawn = capture_area;
    st_buffer->box_currently_drawn = capture_area;

    wl_surface_attach(st_output->surface.wl_surface, st_buffer->wl_buffer, 0, 0);
    wp_presentation_feedback_add_listener(
        wp_presentation_feedback(g_state.globals.presentation, st_output->surface.wl_surface),
        &presentation_feedback_listener,
        st_buffer
    );
    wl_surface_commit(st_output->surface.wl_surface);
done:
    st_output->dirty = false;
}


struct wl_callback_listener surface_frame_callback_listener = {
    .done = surface_frame_callback_handler
};

