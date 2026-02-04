#include <assert.h>
#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "state.h"
#include "event-handlers.h"
#include "util/blend2d.h"


static inline void
_clamp_to_output_width(int *val, struct scran_output *st_output)
{
    if (*val < 0) {
        *val = 0;
    } else if (*val > st_output->mode.width_px) {
        *val = st_output->mode.width_px;
    }
}

static inline void
_clamp_to_output_height(int *val, struct scran_output *st_output)
{
    if (*val < 0) {
        *val = 0;
    } else if (*val > st_output->mode.height_px) {
        *val = st_output->mode.height_px;
    }
}

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

    // TODO: Macro for_each_output ?
    for (int i = 0; i < state->n_outputs; ++i) {
        if (surface_entered == state->outputs[i].surface.surface) {
            pointer_ctx->focused_output = &state->outputs[i];
            break;
        }
    }
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

static void
handle_pointer_motion(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct scran *state = data;
    struct scran_output *st_output = state->seat.pointer_ctx.focused_output;
    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;

    const int x_px = wl_fixed_to_int(x);
    const int y_px = wl_fixed_to_int(y);

    state->seat.pointer_ctx.x_px = x_px;
    state->seat.pointer_ctx.y_px = y_px;

    if (selection_ctx->selection_state == SELECTION_NONE) {
        return;
    }

    // TODO: Check if out of bounds
    assert(!SCRAN_BL_BOX_IS_INVERTED(selection_ctx->bl_box_before_changes));

    switch (selection_ctx->selection_state) {
    case SELECTION_NONE:
        break;
    case SELECTION_IN_PROGRESS:
        // TODO: Merge this with SELECTION_RESIZING?
        // TODO: Make this explicitly either output pixel coordinates or
        //       surface-local coordinates
        //       Also document the behavior/conversion (and make helper function?).
        selection_ctx->bl_box.x1 = x_px;
        selection_ctx->bl_box.y1 = y_px;
        break;
    case SELECTION_COMPLETE:
        break;
    case SELECTION_REBASING:
        {
            int x_diff = x_px - selection_ctx->pointer_before_changes_x_px;
            int y_diff = y_px - selection_ctx->pointer_before_changes_y_px;
            const BLBoxI box_before_rebase = selection_ctx->bl_box_before_changes;

            BLBoxI new_box = {
                .x0 = box_before_rebase.x0 + x_diff,
                .y0 = box_before_rebase.y0 + y_diff,
                .x1 = box_before_rebase.x1 + x_diff,
                .y1 = box_before_rebase.y1 + y_diff,
            };

            // The rebase should have been initiated with a valid box.
            assert(!SCRAN_BL_BOX_IS_INVERTED(box_before_rebase));
            assert(box_before_rebase.x0 >= 0 && box_before_rebase.x1 <= st_output->mode.width_px);
            assert(box_before_rebase.y0 >= 0 && box_before_rebase.y1 <= st_output->mode.height_px);

            // Restrict the area to be within the output's borders.
            // TODO: Maybe make this cleaner ?
            if (new_box.x0 < 0) {
                new_box.x1 -= new_box.x0;
                new_box.x0 = 0;
            } else if (new_box.x1 > st_output->mode.width_px) {
                new_box.x0 -= new_box.x1 - st_output->mode.width_px;
                new_box.x1 = st_output->mode.width_px;
            }
            if (new_box.y0 < 0) {
                new_box.y1 -= new_box.y0;
                new_box.y0 = 0;
            } else if (new_box.y1 > st_output->mode.height_px) {
                new_box.y0 -= new_box.y1 - st_output->mode.height_px;
                new_box.y1 = st_output->mode.height_px;
            }

            selection_ctx->bl_box = new_box;
        }
        break;
    case SELECTION_RESIZING:
        {
            const int x_diff_px = x_px - selection_ctx->pointer_before_changes_x_px;
            const int y_diff_px = y_px - selection_ctx->pointer_before_changes_y_px;
            const BLBoxI box_before_resize = selection_ctx->bl_box_before_changes;

            switch (selection_ctx->selection_resize_direction) {
            case SELECTION_RESIZE_NONE:
                break;
            case SELECTION_RESIZE_TOP_LEFT:
                selection_ctx->bl_box.x0 = box_before_resize.x0 + x_diff_px;
                selection_ctx->bl_box.y0 = box_before_resize.y0 + y_diff_px;
                _clamp_to_output_width(&selection_ctx->bl_box.x0, st_output);
                _clamp_to_output_height(&selection_ctx->bl_box.y0, st_output);
                break;
            case SELECTION_RESIZE_TOP_RIGHT:
                selection_ctx->bl_box.x1 = box_before_resize.x1 + x_diff_px;
                selection_ctx->bl_box.y0 = box_before_resize.y0 + y_diff_px;
                _clamp_to_output_width(&selection_ctx->bl_box.x1, st_output);
                _clamp_to_output_height(&selection_ctx->bl_box.y0, st_output);
                break;
            case SELECTION_RESIZE_BOTTOM_LEFT:
                selection_ctx->bl_box.x0 = box_before_resize.x0 + x_diff_px;
                selection_ctx->bl_box.y1 = box_before_resize.y1 + y_diff_px;
                _clamp_to_output_width(&selection_ctx->bl_box.x0, st_output);
                _clamp_to_output_height(&selection_ctx->bl_box.y1, st_output);
                break;
            case SELECTION_RESIZE_BOTTOM_RIGHT:
                selection_ctx->bl_box.x1 = box_before_resize.x1 + x_diff_px;
                selection_ctx->bl_box.y1 = box_before_resize.y1 + y_diff_px;
                _clamp_to_output_width(&selection_ctx->bl_box.x1, st_output);
                _clamp_to_output_height(&selection_ctx->bl_box.y1, st_output);
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
    struct scran_output *st_output = state->seat.pointer_ctx.focused_output;
    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;

    // TODO: Implement dragging

    int x_px = state->seat.pointer_ctx.x_px;
    int y_px = state->seat.pointer_ctx.y_px;

    // TODO: Add hold/click-and-drag functionality
    if (button_state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }

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

                selection_ctx->bl_box = initial_selection_area;
                for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
                    st_output->surface.double_buffer[i].bl_box_rendered = initial_selection_area;
                }
            }
            selection_ctx->selection_state = SELECTION_IN_PROGRESS;
            break;
        case SELECTION_IN_PROGRESS:
            selection_ctx->bl_box.x1 = x_px;
            selection_ctx->bl_box.y1 = y_px;
            selection_ctx->selection_state = SELECTION_COMPLETE;
            break;
        case SELECTION_COMPLETE:
            selection_ctx->selection_state = SELECTION_REBASING;
            selection_ctx->pointer_before_changes_x_px = x_px;
            selection_ctx->pointer_before_changes_y_px = y_px;
            selection_ctx->bl_box_before_changes = selection_ctx->bl_box;
            break;
        case SELECTION_REBASING:
            selection_ctx->selection_state = SELECTION_COMPLETE;
            break;
        case SELECTION_RESIZING:
            // TODO: Allow BTN_LEFT to stop the resizing ?
            break;
        }
        break;
    case BTN_RIGHT:
        switch(selection_ctx->selection_state) {
        case SELECTION_COMPLETE:
            selection_ctx->selection_state = SELECTION_RESIZING;
            selection_ctx->bl_box_before_changes = selection_ctx->bl_box;
            selection_ctx->pointer_before_changes_x_px = x_px;
            selection_ctx->pointer_before_changes_y_px = y_px;

            {
                const BLBoxI box_before_changes = selection_ctx->bl_box_before_changes;

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
            
            BLBoxI *const box = &selection_ctx->bl_box;
            if (box->x1 < box->x0) {
                int tmp = box->x0;
                box->x0 = box->x1;
                box->x1 = tmp;
            }
            if (box->y1 < box->y0) {
                int tmp = box->y0;
                box->y0 = box->y1;
                box->y1 = tmp;
            }
            break;
        default:
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
    // TODO ?
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

