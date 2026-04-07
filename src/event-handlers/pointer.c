#include <assert.h>
#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "state.h"
#include "state-util.h"
#include "util/blend2d.h"
#include "event-handlers.h"
#include "selection.h"


static void
handle_pointer_enter(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_entered,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct scran *state = data;
    struct scran_seat_pointerContext *pointer_ctx = &state->seat.pointer_ctx;

    // "When a seat's focus enters a surface, the pointer image is undefined..."
    wp_cursor_shape_device_v1_set_shape(
        pointer_ctx->cursor_shape_device,
        serial,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );

    for (int i = 0; i < state->n_outputs; ++i) {
        struct scran_output_surface *st_surface = &state->outputs[i].surface;

        if (surface_entered == st_surface->wl_surface) {
            pointer_ctx->focused_whole_output_layer_surface = st_surface;
            return;
        }
    }

    // XXX: We do not have any other surfaces at the moment, so this should
    // never happen. This was changed to tracking the surface rather than
    // the output to make the scaling code more sane, despite only having one
    // surface per output at the moment. Change this as appropariate if adding
    // more surfaces.
    // We should still handle focused_surface == NULL appropriately in the rest
    // of the code, so this should still not be an error.
    //     XXX: This can currently trigger when dragging out of output bounds
    //     into a second, *left-hand-side* monitor.
    pointer_ctx->focused_whole_output_layer_surface = NULL;
    DEBUG("WARNING: wl_pointer::enter triggered with unknown surface (see comment in source.)\n");
}


static void
handle_pointer_leave(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_left
) {
    // Nothing to do here yet...
}


// We use this mainly to set our capture-area-deciding selection box, and not
// our border-render-deciding box (which lives in our viewport "source buffer",
// and we define based on the capture area), but this function is named like
// this because getting translating to the source buffer coordinates is what
// we are in effect doing (since the source buffer should be +/- 1 pixel
// difference to capture area dimensions, which is the range we're mapping
// when scaling/rounding like we have to do here.
static inline int
_selection_surface_logical_coordinate_to_buffer_pixel(
    struct scran_output_surface *st_surface,
    wl_fixed_t surface_pixel_fixed
) {
    double surface_pixel_double = wl_fixed_to_double(surface_pixel_fixed);
    double scale = st_surface->final_scale_factor_normalized;
    int output_pixel = round(surface_pixel_double * scale);

    return output_pixel;
}


// TODO: Benchmark with/without early exit on unchanged cursor position.
static void
handle_pointer_motion(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    // surface-local coordinates, i.e. must be multiplied by scale factor to
    // convert to viewport source coordinates
    wl_fixed_t x_surface,
    wl_fixed_t y_surface
) {
    struct scran *state = data;

    if (state->seat.pointer_ctx.focused_whole_output_layer_surface == NULL) {
        return;
    }
    struct scran_output_surface *st_surface = state->seat.pointer_ctx.focused_whole_output_layer_surface;

    struct scran_output *st_output = wl_container_of(st_surface, st_output, surface);
    struct scran_seat_pointerContext *pointer_ctx = &state->seat.pointer_ctx;
    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;


    int x_px = _selection_surface_logical_coordinate_to_buffer_pixel(st_surface, x_surface);
    int y_px = _selection_surface_logical_coordinate_to_buffer_pixel(st_surface, y_surface);

    // TODO: Maybe use a BLPoint to neatly pack this into _selection_surface_logical_coordinate_to_buffer_pixel.
    clamp_to_transformed_output_width(&x_px, st_output);
    clamp_to_transformed_output_height(&y_px, st_output);

    pointer_ctx->x_px = x_px;
    pointer_ctx->y_px = y_px;

    if (selection_ctx->selection_state == SELECTION_NONE) {
        return;
    }

    // TODO: Check if out of bounds
    assert(!SCRAN_BL_BOX_IS_INVERTED(selection_ctx->box_before_changes_px));

    switch (selection_ctx->selection_state) {
    case SELECTION_NONE:
        break;
    case SELECTION_INITIALIZING:
        selection_ctx->box_px.x1 = x_px;
        selection_ctx->box_px.y1 = y_px;

        // XXX: Kinda redundant since we must also clamp on finishing selection
        clamp_to_transformed_output_width(&selection_ctx->box_px.x1, st_output);
        clamp_to_transformed_output_height(&selection_ctx->box_px.y1, st_output);

        break;
    case SELECTION_COMPLETE:
    case SELECTION_COMPLETE_FREEZE_SIZE:
        break;
    case SELECTION_REBASING:
    case SELECTION_REBASING_FREEZE_SIZE:
        {
            int x_diff = x_px - selection_ctx->pointer_before_changes_x_px;
            int y_diff = y_px - selection_ctx->pointer_before_changes_y_px;
            const BLBoxI box_before_rebase = selection_ctx->box_before_changes_px;

            BLBoxI new_box = {
                .x0 = box_before_rebase.x0 + x_diff,
                .y0 = box_before_rebase.y0 + y_diff,
                .x1 = box_before_rebase.x1 + x_diff,
                .y1 = box_before_rebase.y1 + y_diff,
            };

            // The rebase should have been initiated with a valid box.
            assert(!SCRAN_BL_BOX_IS_INVERTED(box_before_rebase));
            assert(box_before_rebase.x0 >= 0 && box_before_rebase.x1 <= get_transformed_output_width(st_output));
            assert(box_before_rebase.y0 >= 0 && box_before_rebase.y1 <= get_transformed_output_height(st_output));

            // Restrict the area to be within the output's borders.
            // TODO: Maybe make this cleaner ?
            if (new_box.x0 < 0) {
                new_box.x1 -= new_box.x0;
                new_box.x0 = 0;
            } else if (new_box.x1 > get_transformed_output_width(st_output)) {
                new_box.x0 -= new_box.x1 - get_transformed_output_width(st_output);
                new_box.x1 = get_transformed_output_width(st_output);
            }
            if (new_box.y0 < 0) {
                new_box.y1 -= new_box.y0;
                new_box.y0 = 0;
            } else if (new_box.y1 > get_transformed_output_height(st_output)) {
                new_box.y0 -= new_box.y1 - get_transformed_output_height(st_output);
                new_box.y1 = get_transformed_output_height(st_output);
            }

            selection_ctx->box_px = new_box;
        }
        break;
    case SELECTION_RESIZING:
        {
            const int x_diff_px = x_px - selection_ctx->pointer_before_changes_x_px;
            const int y_diff_px = y_px - selection_ctx->pointer_before_changes_y_px;
            const BLBoxI box_before_resize = selection_ctx->box_before_changes_px;

            switch (selection_ctx->selection_resize_direction) {
            case SELECTION_RESIZE_NONE:
                break;
            case SELECTION_RESIZE_TOP_LEFT:
                selection_ctx->box_px.x0 = box_before_resize.x0 + x_diff_px;
                selection_ctx->box_px.y0 = box_before_resize.y0 + y_diff_px;
                clamp_to_transformed_output_width(&selection_ctx->box_px.x0, st_output);
                clamp_to_transformed_output_height(&selection_ctx->box_px.y0, st_output);
                break;
            case SELECTION_RESIZE_TOP_RIGHT:
                selection_ctx->box_px.x1 = box_before_resize.x1 + x_diff_px;
                selection_ctx->box_px.y0 = box_before_resize.y0 + y_diff_px;
                clamp_to_transformed_output_width(&selection_ctx->box_px.x1, st_output);
                clamp_to_transformed_output_height(&selection_ctx->box_px.y0, st_output);
                break;
            case SELECTION_RESIZE_BOTTOM_LEFT:
                selection_ctx->box_px.x0 = box_before_resize.x0 + x_diff_px;
                selection_ctx->box_px.y1 = box_before_resize.y1 + y_diff_px;
                clamp_to_transformed_output_width(&selection_ctx->box_px.x0, st_output);
                clamp_to_transformed_output_height(&selection_ctx->box_px.y1, st_output);
                break;
            case SELECTION_RESIZE_BOTTOM_RIGHT:
                selection_ctx->box_px.x1 = box_before_resize.x1 + x_diff_px;
                selection_ctx->box_px.y1 = box_before_resize.y1 + y_diff_px;
                clamp_to_transformed_output_width(&selection_ctx->box_px.x1, st_output);
                clamp_to_transformed_output_height(&selection_ctx->box_px.y1, st_output);
                break;
            }
        }
        break;
    }
}


static inline int32_t
get_center_value(int32_t x0, int32_t x1)
{
    return x0 + ((x1 - x0) / 2);
}


static void
handle_pointer_button(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    enum wl_pointer_button_state button_state
) {
    struct scran *state = data;

    if (state->seat.pointer_ctx.focused_whole_output_layer_surface == NULL) {
        return;
    }
    struct scran_output_surface *st_surface = state->seat.pointer_ctx.focused_whole_output_layer_surface;

    struct scran_seat_pointerContext *pointer_ctx = &state->seat.pointer_ctx;

    DEBUG("handle_pointer_button()\n");

    bool is_press   = button_state == WL_POINTER_BUTTON_STATE_PRESSED;
    bool is_release = button_state == WL_POINTER_BUTTON_STATE_RELEASED;
    bool is_active_button = button == pointer_ctx->active_button;
    bool have_some_active_button = pointer_ctx->active_button != SCRAN_BTN_NONE;

    if (pointer_ctx->use_presses_only) {
        assert(pointer_ctx->active_button == SCRAN_BTN_NONE);
        if (!is_press) {
            DEBUG("  Ignored (received non-press with use_presses_only)\n");
            return;
        }
    } else {
        if (is_release && !is_active_button) {
            DEBUG("  Ignored (received release of non-active button_\n");
            return;
        }
        if (is_press && have_some_active_button) {
            DEBUG("  Ignored (received press when already have active button)\n");
            return;
        }
    }

    DEBUG("  Went through (button=%d, state=%b)\n", button, button_state);

    pointer_ctx->active_button = pointer_ctx->active_button ? SCRAN_BTN_NONE : button;

    struct scran_output *st_output = wl_container_of(st_surface, st_output, surface);
    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;

    int x_px = pointer_ctx->x_px;
    int y_px = pointer_ctx->y_px;

    // Should e.g. not be buffer-coordinates (which is sometimes 1px larger than output dimension)
    assert(x_px <= get_transformed_output_width(st_output));
    assert(y_px <= get_transformed_output_height(st_output));

    switch (button) {
    case BTN_LEFT:
        switch(selection_ctx->selection_state) {
        case SELECTION_NONE:
            {
                const struct BLBoxI initial_selection_area = {
                    .x0 = x_px,
                    .y0 = y_px,
                    .x1 = x_px,
                    .y1 = y_px,
                };

                selection_ctx->box_px = initial_selection_area;
            }
            selection_ctx->selection_state = SELECTION_INITIALIZING;
            break;
        case SELECTION_INITIALIZING:
            selection_ctx->box_px.x1 = x_px;
            selection_ctx->box_px.y1 = y_px;

            blboxi_deinvert(&selection_ctx->box_px);

            clamp_to_transformed_output_width(&selection_ctx->box_px.x0, st_output);
            clamp_to_transformed_output_height(&selection_ctx->box_px.y0, st_output);
            clamp_to_transformed_output_width(&selection_ctx->box_px.x1, st_output);
            clamp_to_transformed_output_height(&selection_ctx->box_px.y1, st_output);

            set_selection_initialized(st_output);
            assert(selection_ctx->selection_state == SELECTION_COMPLETE);

            break;
        case SELECTION_COMPLETE:
        case SELECTION_COMPLETE_FREEZE_SIZE:
            selection_ctx->selection_state = selection_ctx->selection_state == SELECTION_COMPLETE
                                             ? SELECTION_REBASING : SELECTION_REBASING_FREEZE_SIZE;
            selection_ctx->pointer_before_changes_x_px = x_px;
            selection_ctx->pointer_before_changes_y_px = y_px;
            selection_ctx->box_before_changes_px = selection_ctx->box_px;
            break;
        case SELECTION_REBASING:
            selection_ctx->selection_state = SELECTION_COMPLETE;
            break;
        case SELECTION_REBASING_FREEZE_SIZE:
            selection_ctx->selection_state = SELECTION_COMPLETE_FREEZE_SIZE;
            break;
        case SELECTION_RESIZING:
            break;
        }
        break;
    case BTN_RIGHT:
        switch(selection_ctx->selection_state) {
        case SELECTION_COMPLETE:
            selection_ctx->selection_state = SELECTION_RESIZING;
            selection_ctx->box_before_changes_px = selection_ctx->box_px;
            selection_ctx->pointer_before_changes_x_px = x_px;
            selection_ctx->pointer_before_changes_y_px = y_px;

            {
                const BLBoxI box_before_changes = selection_ctx->box_before_changes_px;

                // TODO: Make this cleaner.....
                if (x_px < get_center_value(box_before_changes.x0, box_before_changes.x1)) {
                    if (y_px < get_center_value(box_before_changes.y0, box_before_changes.y1)) {
                        selection_ctx->selection_resize_direction = SELECTION_RESIZE_TOP_LEFT;
                    } else {
                        selection_ctx->selection_resize_direction = SELECTION_RESIZE_BOTTOM_LEFT;
                    }
                } else {
                    if (y_px < get_center_value(box_before_changes.y0, box_before_changes.y1)) {
                        selection_ctx->selection_resize_direction = SELECTION_RESIZE_TOP_RIGHT;
                    } else {
                        selection_ctx->selection_resize_direction = SELECTION_RESIZE_BOTTOM_RIGHT;
                    }
                }
            }
            break;
        case SELECTION_RESIZING:
            selection_ctx->selection_state = SELECTION_COMPLETE;
            selection_ctx->selection_resize_direction = SELECTION_RESIZE_NONE;
            
            blboxi_deinvert(&selection_ctx->box_px);
            break;
        default:
            // Shunt back to a safe state to prevent state inversions or other
            // unexpected side-effects.
            pointer_ctx->active_button = SCRAN_BTN_NONE;
            break;
        }
        break;
    }
}


static void
handle_pointer_frame(
    void *data,
    struct wl_pointer *wl_pointer
) {
    // Don't need this yet...
}

static void
handle_pointer_axis(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t time,
    uint32_t axis,
    wl_fixed_t value
) {
    // TODO
}

static void
handle_axis_discrete(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t axis,
    int32_t discrete
) {
    // TODO
}

static void
handle_axis_relative_direction(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t axis,
    uint32_t direction
) {
    // TODO
}

static void
handle_axis_source(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t axis_source
) {
    // TODO
}

static void
handle_axis_stop(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t time,
    uint32_t axis
) {
    // TODO
}

static void
handle_axis_value120(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t axis,
    int32_t value120
) {
    // TODO
}


struct wl_pointer_listener pointer_listener = {
    .enter = handle_pointer_enter,
    .leave = handle_pointer_leave,
    .button = handle_pointer_button,
    .motion = handle_pointer_motion,
    .frame = handle_pointer_frame,
    .axis = handle_pointer_axis,
    .axis_discrete = handle_axis_discrete,
    .axis_relative_direction = handle_axis_relative_direction,
    .axis_source = handle_axis_source,
    .axis_stop = handle_axis_stop,
    .axis_value120 = handle_axis_value120,
};

