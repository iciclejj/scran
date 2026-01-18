#include <linux/input-event-codes.h>
#include <wayland-client.h>

#include "state.h"
#include "event-handlers.h"

static void
handle_pointer_enter(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_entered,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct client_state *state = data;
    struct client_state_seat_pointer *st_pointer = &state->seat.pointer;

    // "When a seat's focus enters a surface, the pointer image is undefined..."
    wp_cursor_shape_device_v1_set_shape(
        st_pointer->cursor_shape_device,
        serial,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );

    // TODO: Macro for_each_output ?
    for (int i = 0; i < state->n_outputs; ++i) {
        if (surface_entered == state->outputs[i].surface.surface) {
            // TODO: Is is_focused useful?
            state->outputs[i].surface.is_focused = true;
            st_pointer->focused_output = &state->outputs[i];
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
    struct client_state *state = data;

    // TODO: reset_selection((struct client_state *)data);

    for (int i = 0; i < state->n_outputs; ++i) {
        if (surface_left == state->outputs[i].surface.surface) {
            // TODO: Is is_focused useful?
            state->outputs[i].surface.is_focused = false;
            break;
        }
    }
}

static void
handle_pointer_motion(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct client_state *state = data;
    struct client_state_output *st_output = state->seat.pointer.focused_output;
    struct client_state_output_selection *st_selection = &st_output->selection;

    const int x_px = wl_fixed_to_int(x);
    const int y_px = wl_fixed_to_int(y);

    state->seat.pointer.x_px = x_px;
    state->seat.pointer.y_px = y_px;

    if (st_selection->selection_state == SELECTION_NONE) {
        return;
    }

    switch (st_selection->selection_state) {
    case SELECTION_NONE:
        break;
    case SELECTION_IN_PROGRESS:
        // TODO: Merge this with SELECTION_RESIZING?
        // TODO: Make this explicitly either output pixel coordinates or
        //       surface-local coordinates
        //       Also document the behavior/conversion (and make helper function?).
        st_selection->bl.box.x1 = x_px;
        st_selection->bl.box.y1 = y_px;
        break;
    case SELECTION_COMPLETE:
        break;
    case SELECTION_REBASING:
        {
            const int x_diff = x_px - st_selection->pointer_origin_rebase_x_px;
            const int y_diff = y_px - st_selection->pointer_origin_rebase_y_px;
            const BLBoxI box_before_rebase = st_selection->bl.box_before_rebase;

            st_selection->bl.box.x0 = box_before_rebase.x0 + x_diff;
            st_selection->bl.box.x1 = box_before_rebase.x1 + x_diff;
            st_selection->bl.box.y0 = box_before_rebase.y0 + y_diff;
            st_selection->bl.box.y1 = box_before_rebase.y1 + y_diff;
        }
        break;
    case SELECTION_RESIZING:
        // TODO: Make this cleaner...
        //       Handle inverted selection
        //           i.e. drag bottom edge past top edge => TOP_LEFT becomes bottom left
        {
            const int x_diff_px = x_px - st_selection->pointer_origin_resize_x_px;
            const int y_diff_px = y_px - st_selection->pointer_origin_resize_y_px;
            const BLBoxI box_before_resize = st_selection->bl.box_before_resize;

            switch (st_selection->selection_resize_direction) {
            case SELECTION_NONE:
                break;
            case SELECTION_RESIZE_TOP_LEFT:
                st_selection->bl.box.x0 = box_before_resize.x0 + x_diff_px;
                st_selection->bl.box.y0 = box_before_resize.y0 + y_diff_px;
                break;
            case SELECTION_RESIZE_TOP_RIGHT:
                st_selection->bl.box.x1 = box_before_resize.x1 + x_diff_px;
                st_selection->bl.box.y0 = box_before_resize.y0 + y_diff_px;
                break;
            case SELECTION_RESIZE_BOTTOM_LEFT:
                st_selection->bl.box.x0 = box_before_resize.x0 + x_diff_px;
                st_selection->bl.box.y1 = box_before_resize.y1 + y_diff_px;
                break;
            case SELECTION_RESIZE_BOTTOM_RIGHT:
                st_selection->bl.box.x1 = box_before_resize.x1 + x_diff_px;
                st_selection->bl.box.y1 = box_before_resize.y1 + y_diff_px;
                break;
            }
        }
        break;
    }

    // TODO: Dynamically resize visual selection
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
    struct client_state *state = data;
    struct client_state_output *st_output = state->seat.pointer.focused_output;
    struct client_state_seat_pointer *st_pointer = &state->seat.pointer;
    struct client_state_output_selection_blend2d *bl = &st_output->selection.bl;

    // TODO: Implement dragging

    int x_px = state->seat.pointer.x_px;
    int y_px = state->seat.pointer.y_px;

    // TODO: Add hold/click-and-drag functionality
    if (button_state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }

    switch (button) {
    case BTN_LEFT:
        switch(st_output->selection.selection_state) {
        case SELECTION_NONE:
            st_output->selection.bl.box.x0 = x_px;
            st_output->selection.bl.box.y0 = y_px;

            // XXX TEST: Might keep, though..
            st_output->selection.bl.box.x1 = x_px;
            st_output->selection.bl.box.y1 = y_px;

            st_output->selection.selection_state = SELECTION_IN_PROGRESS;
            break;
        case SELECTION_IN_PROGRESS:
            st_output->selection.bl.box.x1 = x_px;
            st_output->selection.bl.box.y1 = y_px;
            st_output->selection.selection_state = SELECTION_COMPLETE;
            break;
        case SELECTION_COMPLETE:
            st_output->selection.pointer_origin_rebase_x_px = x_px;
            st_output->selection.pointer_origin_rebase_y_px = y_px;
            st_output->selection.bl.box_before_rebase = st_output->selection.bl.box;
            st_output->selection.selection_state = SELECTION_REBASING;
            break;
        case SELECTION_REBASING:
            st_output->selection.selection_state = SELECTION_COMPLETE;
            break;
        case SELECTION_RESIZING:
            // TODO: Allow BTN_LEFT to stop the resizing ?
            break;
        }
        break;
    case BTN_RIGHT:
        switch(st_output->selection.selection_state) {
        case SELECTION_COMPLETE:
            st_output->selection.selection_state = SELECTION_RESIZING;
            st_output->selection.bl.box_before_resize = st_output->selection.bl.box;

            st_output->selection.pointer_origin_resize_x_px = x_px;
            st_output->selection.pointer_origin_resize_y_px = y_px;

            // TODO: Make this cleaner.....
            if (x_px < get_center_value(st_output->selection.bl.box_before_resize.x0, st_output->selection.bl.box_before_resize.x1)) {
                if (y_px < get_center_value(st_output->selection.bl.box_before_resize.y0, st_output->selection.bl.box_before_resize.y1)) {
                    st_output->selection.selection_resize_direction = SELECTION_RESIZE_TOP_LEFT;
                } else {
                    st_output->selection.selection_resize_direction = SELECTION_RESIZE_BOTTOM_LEFT;
                }
            } else {
                if (y_px < get_center_value(st_output->selection.bl.box_before_resize.y0, st_output->selection.bl.box_before_resize.y1)) {
                    st_output->selection.selection_resize_direction = SELECTION_RESIZE_TOP_RIGHT;
                } else {
                    st_output->selection.selection_resize_direction = SELECTION_RESIZE_BOTTOM_RIGHT;
                }
            }
            break;
        case SELECTION_RESIZING:
            st_output->selection.selection_state = SELECTION_COMPLETE;
            st_output->selection.selection_resize_direction = SELECTION_RESIZE_NONE;
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

struct wl_pointer_listener pointer_listener = {
    .enter = handle_pointer_enter,
    .leave = handle_pointer_leave,
    .button = handle_pointer_button,
    .motion = handle_pointer_motion,
    .frame = handle_pointer_frame,
};

