#include <assert.h>
#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "state.h"
#include "state-util.h"
#include "cursor.h"
#include "seat.h"
#include "selection-surface.h"
#include "ui.h"
#include "util/blend2d.h"
#include "event-handlers.h"
#include "selection.h"


static void
handle_pointer_enter(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_entered,
    wl_fixed_t x_surface,
    wl_fixed_t y_surface
) {
    struct scran *state = data;
    struct scran_seat_pointerContext *pointer_ctx = &state->seat.pointer_ctx;

    pointer_ctx->last_enter_serial = serial;

    struct scran_output_selectionSurface *pointer_surface =
        seat_update_pointer_focus(&state->seat, surface_entered, x_surface, y_surface);

    if (!pointer_surface) {
        return;
    }

    struct scran_output *st_output = wl_container_of(pointer_ctx->focused_selection_surface, st_output, selection_surface);

    // "When a seat's focus enters a surface, the pointer image is undefined..."
    cursor_set_theme(st_output, st_output->cursor.theme);
}


static void
handle_pointer_leave(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface_left
) {
    struct scran *state = data;
    seat_update_pointer_focus(&state->seat, NULL, 0, 0);
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
    struct scran_output_selectionSurface *focused_selection_surface = state->seat.pointer_ctx.focused_selection_surface;

    if (focused_selection_surface == NULL) {
        return;
    }

    struct scran_output *st_output = wl_container_of(focused_selection_surface, st_output, selection_surface);
    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;

    BLPointI coords_px_ = seat_update_pointer_coordinates(&state->seat, x_surface, y_surface);
    int x_px = coords_px_.x;
    int y_px = coords_px_.y;

    // TODO: Check if out of bounds
    assert(!blboxi_is_inverted(selection_ctx->box_before_changes_px));

    switch (selection_ctx->selection_state) {
    case SELECTION_NONE:
    case SELECTION_NONE_FREEZE_SIZE:
        break;
    case SELECTION_INITIALIZING:
        selection_ctx->box_px.x1 = x_px;
        selection_ctx->box_px.y1 = y_px;

        // TODO: Merge most of this logic with SELECTION_RESIZING?

        // XXX: Kinda redundant since we must also clamp on finishing selection
        clamp_to_transformed_output_width(&selection_ctx->box_px.x1, st_output);
        clamp_to_transformed_output_height(&selection_ctx->box_px.y1, st_output);

        scran_ui_statusline_set_selection_size(&st_output->selection_surface.ui_ctx.ui_statusline, blboxi_to_blrecti(selection_ctx->box_px));

        request_selection_surface_frame_callback(st_output);
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
            assert(!blboxi_is_inverted(box_before_rebase));
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
        request_selection_surface_frame_callback(st_output);
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
        scran_ui_statusline_set_selection_size(&st_output->selection_surface.ui_ctx.ui_statusline, blboxi_to_blrecti(selection_ctx->box_px));
        request_selection_surface_frame_callback(st_output);
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
    struct scran_output_selectionSurface *focused_selection_surface = state->seat.pointer_ctx.focused_selection_surface;

    if (focused_selection_surface == NULL) {
        return;
    }

    if (!state->seat.pointer_ctx.pointer_focus_trusted && button_state != WL_POINTER_BUTTON_STATE_RELEASED) {
        print_untrusted_active_surface_message();
        return;
    }


    struct scran_seat_pointerContext *pointer_ctx = &state->seat.pointer_ctx;

    bool is_press   = button_state == WL_POINTER_BUTTON_STATE_PRESSED;
    bool is_release = button_state == WL_POINTER_BUTTON_STATE_RELEASED;
    bool is_active_button = button == pointer_ctx->active_button;
    bool have_some_active_button = pointer_ctx->active_button != SCRAN_BTN_NONE;

    if (pointer_ctx->use_presses_only) {
        assert(pointer_ctx->active_button == SCRAN_BTN_NONE);
        if (!is_press) {
            DEBUG("Ignored pointer button release (use_presses_only)\n");
            return;
        }
    } else {
        if (is_release && !is_active_button) {
            DEBUG("Ignored pointer button release (!is_active_button)\n");
            return;
        }
        if (is_press && have_some_active_button) {
            DEBUG("Ignored pointer button press (another button is active)\n");
            return;
        }
    }

    DEBUG("Accepted pointer button event (button=%d, state=%b)\n", button, button_state);

    pointer_ctx->active_button = pointer_ctx->active_button ? SCRAN_BTN_NONE : button;

    struct scran_output *st_output = wl_container_of(focused_selection_surface, st_output, selection_surface);
    struct scran_output_selectionContext *selection_ctx = &st_output->selection_ctx;

    BLPointI coords_px_ = seat_get_pointer_coordinates(&state->seat);
    int x_px = coords_px_.x;
    int y_px = coords_px_.y;

    // Should e.g. not be buffer-coordinates (which is sometimes 1px larger than output dimension)
    assert(x_px <= get_transformed_output_width(st_output));
    assert(y_px <= get_transformed_output_height(st_output));

    switch (button) {
    case BTN_LEFT:
        switch(selection_ctx->selection_state) {
        case SELECTION_NONE_FREEZE_SIZE:
            break;
        case SELECTION_NONE:
            ;
            const struct BLBoxI initial_selection_area = {
                .x0 = x_px,
                .y0 = y_px,
                .x1 = x_px,
                .y1 = y_px,
            };
            selection_ctx->box_px = initial_selection_area;
            selection_ctx->selection_state = SELECTION_INITIALIZING;

            // TODO: Create set_selection_initializing()/set_selection_stage(),
            // analogous to current set_selection_initialized()?
            scran_ui_statusline_set_selection_size(
                &st_output->selection_surface.ui_ctx.ui_statusline,
                blboxi_to_blrecti(initial_selection_area)
            );

            set_selection_surface_theme(st_output, SURFACE_THEME_DEFAULT);

            request_selection_surface_frame_callback(st_output);

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
