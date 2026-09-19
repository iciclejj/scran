#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <uchar.h>

#include <blend2d/blend2d.h>

#include "selection.h"
#include "state.h"
#include "selection-surface.h"
#include "capture.h"
#include "dbus.h"
#include "init.h"
#include "scran-ui-text.h"
#include "util/blend2d.h"
#include "event-handlers.h"
#include "print.h"
#include "atlas.h"
#include "util/util.h"


static inline struct atlas_text_metrics
get_item_spacing(const struct atlas *atlas) {
    return (struct atlas_text_metrics) {
        .advance.x = round(3 * atlas->space_glyph_advance.x),
    };
}

static inline void
draw_and_damage_region(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    // Wayland needs to be damaged with difference with the previously drawn box,
    // regardless of which buffer produced it. The buffer itself, on the other
    // hand, must be damaged relative to itself. Marking Wayland damage does
    // not guarantee that no other part of the buffer gets blitted, which is
    // why we must make this distinction.
    //
    // The relevant wl_surface::damage_buffer requirements stated by the xml
    // spec, and with no further guarantees being made, is:
    //   "This request is used to describe the regions **where the pending
    //    buffer is different from the current surface contents** [...]".
    BLRectI damage_region_wayland,
    BLRectI damage_region_buffer
) {
    // TODO: Is bl_context_clear_all the same as bl_context_clear_rect_i, if we do it after clipping?
    bl_context_clip_to_rect_i(&st_buffer->bl_ctx, &damage_region_buffer);
    bl_context_clear_all(&st_buffer->bl_ctx);
    bl_context_fill_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &selection_surface->bl_path);
    bl_context_restore_clipping(&st_buffer->bl_ctx);

    wl_surface_damage_buffer(selection_surface->surface.wl_surface,
        damage_region_wayland.x, damage_region_wayland.y, damage_region_wayland.w, damage_region_wayland.h
    );
}

// NOTE: _draw_and_damage_selection_border must have its damage_regions_buffer
// set accurately (when called after _draw_and_damage_background), since it is
// used to ensure that we don't erase the background we just drew.
// I.e. would not make sense to pass a 'damage_region_everything' even here,
// even as part of a force-redraw.
static inline void
draw_and_damage_selection_border(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area,
    BLBoxI capture_area_border_outline,
    BLBoxI capture_area_border_inline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_inline,  BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);

    for (int i = 0; i < n_damage_regions; ++i) {
        draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    bl_path_clear(&selection_surface->bl_path);

    st_buffer->box_currently_drawn = capture_area;
}

static inline void
draw_and_damage_background(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_max_bounds,
    BLBoxI capture_area_border_outline,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions, // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
    bool greeting_screen
) {
    // TODO: Just store the fill styles in state
    BLVarCore prev_fill_style = { };
    bl_context_get_fill_style(&st_buffer->bl_ctx, &prev_fill_style);

    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, UI_COLOR_BG_DIM);

    bl_path_add_box_i(&selection_surface->bl_path, &capture_area_max_bounds,     BL_GEOMETRY_DIRECTION_NONE);
    if (!greeting_screen) { // TODO: likely()
        bl_path_add_box_i(&selection_surface->bl_path, &capture_area_border_outline, BL_GEOMETRY_DIRECTION_NONE);
    }

    for (int i = 0; i < n_damage_regions; ++i) {
        draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    uint32_t prev_fill_style_rgba32;
    bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);

    bl_path_clear(&selection_surface->bl_path);
}


static inline BLRectI
geometry_to_surface_rect_px(
    const struct scran_output_selectionSurface *selection_surface,
    const struct ui_item_geometry *geometry
) {
    return (BLRectI){
        .x = geometry->pen_origin.x + geometry->text_metrics.bbox.x0,
        .y = geometry->pen_origin.y, // TODO: Track vertical glyph bounds.
        .w = atlas_metrics_bbox_width(&geometry->text_metrics),
        .h = atlas_font_height_px(&selection_surface->atlas),
    };
}

static void
clear_old_ui_item(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    const struct ui_item_geometry *geometry
) {
    const BLRectI text_rect = geometry_to_surface_rect_px(selection_surface, geometry);
    if (text_rect.w <= 0 || text_rect.h <= 0) {
        return;
    }

    BLVarCore prev_fill_style = { };
    bl_context_get_fill_style(&st_buffer->bl_ctx, &prev_fill_style);
    const BLCompOp prev_comp_op = bl_context_get_comp_op(&st_buffer->bl_ctx);

    bl_context_set_comp_op(&st_buffer->bl_ctx, BL_COMP_OP_SRC_COPY);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, UI_COLOR_BG_DIM);

    // Do not overwrite the current transparent capture area or its border.
    // Background/border drawing has already updated any old text pixels there.
    BLRectI uncovered[4];
    blboxi_get_difference_as_4_rects(blrecti_to_blboxi(text_rect), capture_area_border_outline, uncovered);
    for (size_t i = 0; i < ARRAY_LENGTH(uncovered); ++i) {
        if (uncovered[i].w > 0 && uncovered[i].h > 0) {
            bl_context_fill_rect_i(&st_buffer->bl_ctx, &uncovered[i]);
        }
    }

    bl_context_set_comp_op(&st_buffer->bl_ctx, prev_comp_op);
    uint32_t prev_fill_style_rgba32;
    bl_var_to_rgba32(&prev_fill_style, &prev_fill_style_rgba32);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, prev_fill_style_rgba32);
}

static void
damage_ui_item(
    struct scran_output_selectionSurface *selection_surface,
    const struct ui_item_geometry *geometry
) {
    const BLRectI rect = geometry_to_surface_rect_px(selection_surface, geometry);
    if (rect.w > 0 && rect.h > 0) {
        wl_surface_damage_buffer(selection_surface->surface.wl_surface, rect.x, rect.y, rect.w, rect.h);
    }
}

enum scran_horizontal_alignment {
    SCRAN_ALIGN_LEFT,
    SCRAN_ALIGN_RIGHT,
};

enum scran_vertical_placement {
    SCRAN_PLACE_ABOVE,
    SCRAN_PLACE_BELOW,
};

// Returned geometry contains the text pen origin. The ink can begin to its
// left when the first glyph has a negative left-side bearing.
static struct ui_item_geometry
get_ui_item_geometry(
    const BLBoxI *capture_area_border_outline,
    int surface_width_px,
    const struct atlas_text_metrics *textline_metrics,
    int height_px,
    enum scran_horizontal_alignment alignment,
    enum scran_vertical_placement placement
) {
    const int advance_px = atlas_metrics_advance_x_px(textline_metrics);

    int origin_x = alignment == SCRAN_ALIGN_LEFT
        ? capture_area_border_outline->x0
        : capture_area_border_outline->x1 - advance_px;

    if (origin_x < capture_area_border_outline->x0) {
        origin_x = capture_area_border_outline->x0;
    }
    if (origin_x + advance_px > surface_width_px && advance_px <= surface_width_px) {
        origin_x = surface_width_px - advance_px;
    }

    const int origin_y = placement == SCRAN_PLACE_ABOVE
        ? capture_area_border_outline->y0 - height_px
        : capture_area_border_outline->y1;

    return (struct ui_item_geometry) {
        .pen_origin = { origin_x, origin_y },
        .text_metrics = *textline_metrics,
    };
}

static inline bool
ui_item_geometry_equal(
    const struct ui_item_geometry *a,
    const struct ui_item_geometry *b
) {
    return
        blpointi_are_equal(a->pen_origin, b->pen_origin)
        && atlas_metrics_equal(&a->text_metrics, &b->text_metrics);
}

static struct atlas_text_metrics
measure_ui_line(
    const struct atlas *atlas,
    const struct atlas_blit_data *blit_data,
    size_t n_items
) {
    struct atlas_text_metrics line_metrics = {0};

    for (size_t i = 0; i < n_items; ++i) {
        if (i > 0) {
            const struct atlas_text_metrics spacing = get_item_spacing(atlas);
            atlas_append_text_metrics(&line_metrics, &spacing);
        }
        const struct atlas_text_metrics item_metrics = atlas_get_text_metrics_px(atlas, &blit_data[i].string);
        atlas_append_text_metrics(&line_metrics, &item_metrics);
    }

    return line_metrics;
}

static struct atlas_text_metrics
blit_ui_line(
    const struct atlas *atlas,
    BLContextCore *bl_ctx,
    const BLPointI *origin,
    const struct atlas_blit_data *blit_data,
    size_t n_items
) {
    struct atlas_text_metrics line_metrics = {0};

    for (size_t i = 0; i < n_items; ++i) {
        if (i > 0) {
            const struct atlas_text_metrics spacing = get_item_spacing(atlas);
            atlas_append_text_metrics(&line_metrics, &spacing);
        }

        const BLPointI item_origin = {
            .x = origin->x + atlas_metrics_advance_x_px(&line_metrics),
            .y = origin->y,
        };
        const struct atlas_text_metrics item_metrics = atlas_blit_string(atlas, bl_ctx, &item_origin, &blit_data[i]);
        atlas_append_text_metrics(&line_metrics, &item_metrics);
    }

    return line_metrics;
}

static void
update_and_damage_ui_line(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area_border_outline,
    bool buffer_contents_changed,
    const struct ui_item_geometry *buffer_geometry,
    bool surface_contents_changed,
    const struct ui_item_geometry *surface_geometry,
    const struct ui_item_geometry *new_geometry,
    const struct atlas_blit_data *blit_data,
    size_t n_items
) {
    if (buffer_contents_changed) {
        // Force-redraw clears the entire buffer before arriving here,
        // so we don't need to clear it again.
        if (!st_buffer->force_redraw) {
            clear_old_ui_item(selection_surface, st_buffer, capture_area_border_outline, buffer_geometry);
        }

        const struct atlas_text_metrics blit_metrics = blit_ui_line(&selection_surface->atlas, &st_buffer->bl_ctx, &new_geometry->pen_origin, blit_data, n_items);
        assert(atlas_metrics_equal(&blit_metrics, &new_geometry->text_metrics));
        (void)blit_metrics;
    }

    if (surface_contents_changed) {
        // Damage the compositor's old rendered geometry
        damage_ui_item(selection_surface, surface_geometry);
        // Damage the new/desired geometry
        damage_ui_item(selection_surface, new_geometry);
    }
}

static inline void
fill_char16(char16_t *string, size_t length, char16_t character) {
    for (size_t i = 0; i < length; ++i) {
        string[i] = character;
    }
}

// Returns final cursor location
static char16_t *
append_char16_uint(char16_t *cursor, char16_t *right_bound, uint32_t uint_)
{
    char16_t *const start = cursor;

    // Generate the number in reverse
    while (cursor < right_bound) {
        *(cursor++) = u'0' + uint_ % 10;
        uint_ /= 10;

        if (!uint_) {
            break;
        }
    }

    char16_t *const end = cursor;

    // Reverse it
    char16_t *left  = start;
    char16_t *right = end - 1;
    while (right - left >= 1) {
        char16_t tmp = *left;
        *left++  = *right;
        *right-- = tmp;
    }

    return end;
}

static inline char16_t *
prepend_char16_uint_two_digits(char16_t *cursor, uint32_t value) {
    *--cursor = u'0' + value % 10;
    *--cursor = u'0' + value / 10;
    return cursor;
}

#define TIMER_STRLEN          CHAR16_STRLEN(g_ui_strings.statusline_timer_dummy)
#define SELECTION_SIZE_STRLEN CHAR16_STRLEN(g_ui_strings.statusline_selection_size_dummy)

static void
get_timer_string(
    char16_t string[TIMER_STRLEN],
    int total_seconds
) {
    assert(total_seconds >= 0);
    fill_char16(string, TIMER_STRLEN, u' ');
    char16_t *cursor = string + TIMER_STRLEN;

    int hours = total_seconds / 3600;
    int minutes = (total_seconds / 60) % 60;
    const int seconds = total_seconds % 60;

    if (hours > 99) {
        hours = 99;
        minutes = 59;
    }

    cursor = prepend_char16_uint_two_digits(cursor, seconds);
    *--cursor = u':';
    cursor = prepend_char16_uint_two_digits(cursor, minutes);

    if (hours) {
        *--cursor = u':';
        prepend_char16_uint_two_digits(cursor, hours);
    }
}

static void
get_selection_size_string(
    char16_t string[SELECTION_SIZE_STRLEN],
    const struct ui_statusline_content *state
) {
    assert(state->selection_width >= 0);
    assert(state->selection_height >= 0);

    fill_char16(string, SELECTION_SIZE_STRLEN, u' ');
    char16_t *cursor = string;
    char16_t *const right_bound = string + SELECTION_SIZE_STRLEN;

    cursor = append_char16_uint(cursor, right_bound, state->selection_width);
    if (cursor < right_bound) {
        *cursor++ = u'x';
    }
    append_char16_uint(cursor, right_bound, state->selection_height);
}

static inline bool
greeting_content_equal(const struct ui_greeting_content *a, const struct ui_greeting_content *b) {
    return a->visible == b->visible;
}

static inline bool
atlas_blit_data_equal(const struct atlas_blit_data *a, const struct atlas_blit_data *b) {
    return
        a->color == b->color
        && a->string.str == b->string.str
        && a->string.strlen == b->string.strlen;
}

static bool
keymap_content_equal(const struct ui_keymap_content *a, const struct ui_keymap_content *b) {
    for (size_t i = 0; i < UI_KEYMAP_N_ITEMS; ++i) {
        if (!atlas_blit_data_equal(&a->blit_data[i], &b->blit_data[i])) {
            return false;
        }
    }
    return true;
}

static inline bool
statusline_content_equal(const struct ui_statusline_content *a, const struct ui_statusline_content *b) {
    return
        a->selection_width == b->selection_width
        && a->selection_height == b->selection_height
        && a->timer_seconds == b->timer_seconds;
}

static inline bool
greeting_description_equal(const struct ui_greeting_description *a, const struct ui_greeting_description *b) {
    return
        greeting_content_equal(&a->content, &b->content)
        && ui_item_geometry_equal(&a->geometry, &b->geometry);
}

static inline bool
keymap_description_equal(const struct ui_keymap_description *a, const struct ui_keymap_description *b) {
    return
        keymap_content_equal(&a->content, &b->content)
        && ui_item_geometry_equal(&a->geometry, &b->geometry);
}

static inline bool
statusline_description_equal(const struct ui_statusline_description *a, const struct ui_statusline_description *b) {
    return
        statusline_content_equal(&a->content, &b->content)
        && ui_item_geometry_equal(&a->geometry, &b->geometry);
}

static struct ui_keymap_content
collect_keymap_content(struct scran_output *output) {
    const bool surface_focused = g_state.seat.active_selection_surface == &output->selection_surface;
    const bool modifier_active = surface_focused && g_state.seat.mod_key_active;
    const bool video_is_live = capture_video_is_live(output);
    const bool video_audio_disabled =
        video_is_live
        ? !output->capture.audio_active
        : output->capture.audio_disable_modifier_active || g_state.options.disable_audio_capture;

    struct ui_keymap_content content = {
        .blit_data = {
            [UI_KEYMAP_ITEM_IMAGE] = {
                .color = modifier_active ? UI_COLOR_KEYBOARD_MODIFIER : UI_COLOR_TEXT_DEFAULT,
                .string =
                    modifier_active
                    ? UI_STRING(g_ui_strings.keymap_image_mod)
                    : UI_STRING(g_ui_strings.keymap_image_default),
            },
            [UI_KEYMAP_ITEM_VIDEO] = {
                .color =
                    video_is_live
                    ? UI_COLOR_VIDEO_CAPTURE
                    : modifier_active
                        ? UI_COLOR_KEYBOARD_MODIFIER
                        : UI_COLOR_TEXT_DEFAULT,
                .string =
                    video_audio_disabled
                    ? UI_STRING(g_ui_strings.keymap_video_mod)
                    : UI_STRING(g_ui_strings.keymap_video_default),
            },
            [UI_KEYMAP_ITEM_FREEZEFRAME] = {
                .color = output->freezeframe.showing ? UI_COLOR_FREEZEFRAME : UI_COLOR_TEXT_DEFAULT,
                .string =
                    output->freezeframe.showing
                    ? UI_STRING(g_ui_strings.keymap_freezeframe_turn_off)
                    : UI_STRING(g_ui_strings.keymap_freezeframe_turn_on),
            },
            [UI_KEYMAP_ITEM_FOCUS] = {
                .color = UI_COLOR_TEXT_DEFAULT,
                .string =
                    g_state.focused
                    ? UI_STRING(g_ui_strings.keymap_focus_default)
                    : scran_tray_is_registered()
                        ? UI_STRING(g_ui_strings.keymap_focus_released_tray)
                        : UI_STRING(g_ui_strings.keymap_focus_released_help),
            },
        },
    };

    const bool item_pressed[ARRAY_LENGTH(content.blit_data)] = {
        [UI_KEYMAP_ITEM_IMAGE]       = g_state.seat.keyboard.pressed_keys.image,
        [UI_KEYMAP_ITEM_VIDEO]       = g_state.seat.keyboard.pressed_keys.video,
        [UI_KEYMAP_ITEM_FREEZEFRAME] = g_state.seat.keyboard.pressed_keys.freezeframe,
    };

    const bool dim_all_items = !surface_focused;
    const float color_multiplier = dim_all_items ? 0.64f : 0.80f;

    for (size_t i = 0; i < ARRAY_LENGTH(content.blit_data); ++i) {
        if (dim_all_items || item_pressed[i]) {
            content.blit_data[i].color = blrgba32_scale_channels(content.blit_data[i].color, color_multiplier);
        }
    }

    return content;
}

static struct ui_statusline_content
collect_statusline_content(
    struct scran_output *output,
    BLBoxI capture_area,
    bool greeting_screen
) {
    const BLPointI selection = blboxi_get_dimensions(
        greeting_screen
        ? get_fullscreen_selection_box(output)
        : capture_area
    );

    return (struct ui_statusline_content) {
        .selection_width = selection.x,
        .selection_height = selection.y,
        .timer_seconds = get_video_timer_seconds(output, capture_clock_gettime_nsec()),
    };
}

static void
draw_and_damage_ui(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI capture_area,
    BLBoxI capture_area_border_outline,
    bool greeting_screen
) {
    const struct atlas  *atlas  = &selection_surface->atlas;
    struct scran_output *output = wl_container_of(selection_surface, output, selection_surface);
    const int item_height_px  = atlas_font_height_px(atlas);
    const int buffer_width_px = selection_surface->surface.width_px_buffer;

    // Draw the below-selection keymap.
    {
        const struct ui_keymap_content content = collect_keymap_content(output);

        const struct atlas_text_metrics metrics = measure_ui_line(atlas, content.blit_data, ARRAY_LENGTH(content.blit_data));

        const struct ui_keymap_description description = {
            .geometry = get_ui_item_geometry(
                &capture_area_border_outline, buffer_width_px, &metrics, item_height_px, SCRAN_ALIGN_LEFT, SCRAN_PLACE_BELOW
            ),
            .content = content,
        };
        const struct ui_keymap_description *description_in_buffer = &st_buffer->ui.keymap;
        const struct ui_keymap_description *description_in_surface = &selection_surface->ui_last_committed.keymap;

        const bool buffer_changed = st_buffer->force_redraw || !keymap_description_equal(description_in_buffer, &description);
        const bool surface_changed = !keymap_description_equal(description_in_surface, &description);

        update_and_damage_ui_line(
            selection_surface, st_buffer, capture_area_border_outline,
            buffer_changed, &description_in_buffer->geometry,
            surface_changed, &description_in_surface->geometry,
            &description.geometry, content.blit_data, ARRAY_LENGTH(content.blit_data)
        );
        st_buffer->ui.keymap = description;
        selection_surface->ui_last_committed.keymap = description;
    }

    // Draw the above-selection selection size and recording timer.
    {
        const struct ui_statusline_content content = collect_statusline_content(output, capture_area, greeting_screen);

        char16_t selection_size[SELECTION_SIZE_STRLEN];
        char16_t timer[TIMER_STRLEN];
        get_selection_size_string(selection_size, &content);
        get_timer_string(timer, content.timer_seconds);

        const struct atlas_blit_data blit_data[] = {
            {
                .string = { .str = selection_size, .strlen = ARRAY_LENGTH(selection_size) },
                .color =  UI_COLOR_TEXT_DEFAULT,
            }, {
                .string = { .str = timer,          .strlen = ARRAY_LENGTH(timer) },
                .color =  UI_COLOR_TEXT_DEFAULT,
            },
        };
        const struct atlas_text_metrics metrics = measure_ui_line(atlas, blit_data, ARRAY_LENGTH(blit_data));

        const struct ui_statusline_description description = {
            .geometry = get_ui_item_geometry(
                &capture_area_border_outline, buffer_width_px, &metrics, item_height_px, SCRAN_ALIGN_RIGHT, SCRAN_PLACE_ABOVE
            ),
            .content = content,
        };
        const struct ui_statusline_description *description_in_buffer = &st_buffer->ui.statusline;
        const struct ui_statusline_description *description_in_surface = &selection_surface->ui_last_committed.statusline;

        const bool buffer_changed = st_buffer->force_redraw || !statusline_description_equal(description_in_buffer, &description);
        const bool surface_changed = !statusline_description_equal(description_in_surface, &description);

        update_and_damage_ui_line(
            selection_surface, st_buffer, capture_area_border_outline,
            buffer_changed, &description_in_buffer->geometry,
            surface_changed, &description_in_surface->geometry,
            &description.geometry, blit_data, ARRAY_LENGTH(blit_data)
        );

        st_buffer->ui.statusline = description;
        selection_surface->ui_last_committed.statusline = description;
    }

    // Greeting
    {
        const struct ui_greeting_content content = {
            .visible = greeting_screen
        };

        struct ui_greeting_description description = {
            .content = content,
        };

        const struct atlas_blit_data blit_data[] = {
            {
                .string = content.visible ? UI_STRING(g_ui_strings.greeting) : UI_STRING(g_ui_strings.empty),
                .color = UI_COLOR_TEXT_DEFAULT,
            },
        };

        if (content.visible) {
            const struct atlas_text_metrics metrics = measure_ui_line(atlas, blit_data, ARRAY_LENGTH(blit_data));
            description.geometry = get_ui_item_geometry(
                &capture_area_border_outline,
                buffer_width_px,
                &metrics,
                item_height_px,
                SCRAN_ALIGN_LEFT,
                SCRAN_PLACE_ABOVE
            );
            description.geometry.pen_origin.y -= item_height_px;
        }

        const struct ui_greeting_description *description_in_buffer = &st_buffer->ui.greeting;
        const struct ui_greeting_description *description_in_surface = &selection_surface->ui_last_committed.greeting;

        const bool buffer_changed = st_buffer->force_redraw || !greeting_description_equal(description_in_buffer, &description);
        const bool surface_changed = !greeting_description_equal(description_in_surface, &description);

        update_and_damage_ui_line(
            selection_surface, st_buffer, capture_area_border_outline,
            buffer_changed,
            &description_in_buffer->geometry,
            surface_changed,
            &description_in_surface->geometry, &description.geometry,
            blit_data, ARRAY_LENGTH(blit_data)
        );
        st_buffer->ui.greeting = description;
        selection_surface->ui_last_committed.greeting = description;
    }
}

// We trunc/ceil like this to make sure that fractionally scaled displays
// will not be able to bleed our capture border into the captured frame,
// not matter how they do their rounding/down-/upscaling.
// This does make our frame not always pixel-perfect with fractional scaling,
// but should not affect non-scaled displays.
static inline BLBoxI
get_scalesafe_border_inline(
    BLBoxI border_inline,
    double normalized_scale_factor
) {
    return (BLBoxI) {
        .x0 = trunc(trunc(border_inline.x0 / normalized_scale_factor) * normalized_scale_factor),
        .y0 = trunc(trunc(border_inline.y0 / normalized_scale_factor) * normalized_scale_factor),
        .x1 = ceil( ceil( border_inline.x1 / normalized_scale_factor) * normalized_scale_factor),
        .y1 = ceil( ceil( border_inline.y1 / normalized_scale_factor) * normalized_scale_factor),
    };
}

static inline BLBoxI
get_border_outline_from_inline(BLBoxI border_inline) {
    return blboxi_get_inflated(border_inline, SCRAN_SELECTION_BORDER_THICKNESS_PX);
}

void
draw_selection_and_damage_buffer(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    struct scran_output_selectionContext *selection_ctx,
    struct BLBoxI capture_area
) {
    if (g_state.options.hide_ui_level >= SCRAN_OPT_HIDE_UI_EVERYTHING) {
        // XXX: Slightly spaghetti, but required for updating the capture area.
        st_buffer->box_currently_drawn = capture_area;
        return;
    }

    // TODO: Assert bl_ctx has already begun

    // What the compositor has to overwrite:
    const struct BLBoxI capture_area_last_used_in_any_buffer = selection_surface->box_last_drawn;
    // What we have to overwrite:
    const struct BLBoxI capture_area_last_used_in_current_buffer = st_buffer->box_currently_drawn;

    assert(!blboxi_is_inverted(capture_area));
    assert(!blboxi_is_inverted(capture_area_last_used_in_any_buffer));
    // TODO: Assert box_bounds fully surrounds box_to_draw

    const struct BLBoxI capture_area_bounds = {
        0,
        0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer,
    };

    const double scale = selection_surface->surface.final_scale_factor_normalized;

    // TODO: Maybe make this all more readable and not 200 columns wide...

    const BLBoxI capture_area_border_inline                             = get_scalesafe_border_inline(capture_area                            , scale);
    const BLBoxI capture_area_border_inline_last_used_in_any_buffer     = get_scalesafe_border_inline(capture_area_last_used_in_any_buffer    , scale);
    const BLBoxI capture_area_border_inline_last_used_in_current_buffer = get_scalesafe_border_inline(capture_area_last_used_in_current_buffer, scale);

    // XXX: Remake the "stroke width" macros
    const BLBoxI capture_area_border_outline                             = get_border_outline_from_inline(capture_area_border_inline);
    const BLBoxI capture_area_border_outline_last_used_in_any_buffer     = get_border_outline_from_inline(capture_area_border_inline_last_used_in_any_buffer);
    const BLBoxI capture_area_border_outline_last_used_in_current_buffer = get_border_outline_from_inline(capture_area_border_inline_last_used_in_current_buffer);

    // TODO: Make helper wrapper for this so we don't need to pass this bool around.
    bool greeting_screen = selection_is_none(selection_ctx);
    bool selection_changed =
        !blboxi_are_equal(capture_area, st_buffer->box_currently_drawn)
        || !blboxi_are_equal(capture_area, selection_surface->box_last_drawn);

    // Draw background dim
    if (selection_changed || st_buffer->force_redraw) {
        int n_damage_regions;
        BLRectI damage_regions_wayland[8];
        BLRectI damage_regions_buffer[8];

        // TODO: Just do redraw/damage directly whenever we need to redraw, rather than
        // needing to branch within this function?
        if (st_buffer->force_redraw) {
            const BLRectI damage_region_everything = blboxi_to_blrecti(capture_area_bounds);
            damage_regions_wayland[0] = damage_region_everything;
            damage_regions_buffer[0] = damage_region_everything;
            n_damage_regions = 1;
        } else {
            static const int i_background_diffs = 0;
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_outline                           , damage_regions_wayland + i_background_diffs);
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_outline                           , damage_regions_buffer  + i_background_diffs);
            static const int i_old_border_diffs = 4;
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_any_buffer    , capture_area_border_inline_last_used_in_any_buffer    , damage_regions_wayland + i_old_border_diffs);
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline_last_used_in_current_buffer, capture_area_border_inline_last_used_in_current_buffer, damage_regions_buffer  + i_old_border_diffs);
            n_damage_regions = 8;
        }

        draw_and_damage_background(selection_surface, st_buffer, capture_area_bounds, capture_area_border_outline, damage_regions_wayland, damage_regions_buffer, n_damage_regions, greeting_screen);
    }

    if (g_state.options.hide_ui_level < SCRAN_OPT_HIDE_UI_ITEMS) {
        // UI items must be drawn after/on top of the background.
        draw_and_damage_ui(selection_surface, st_buffer, capture_area, capture_area_border_outline, greeting_screen);
    }

    // Draw selection border
    if (selection_changed || st_buffer->force_redraw) {
        if (greeting_screen) { // TODO: unlikely()
            st_buffer->box_currently_drawn = capture_area;
        } else {
            BLRectI damage_regions[4];
            blboxi_get_symmetric_difference_as_4_rects(capture_area_border_outline, capture_area_border_inline, damage_regions);
            draw_and_damage_selection_border(selection_surface, st_buffer, capture_area, capture_area_border_outline, capture_area_border_inline, damage_regions, damage_regions, 4);
        }
    }


    // NOTE: Don't reset the BLContext here, unless intending to fully
    // re-initialize it. Its state is initialized outside of this ::frame
    // event loop. Shouldn't need flushing either unless doing async.
    bl_context_flush(&st_buffer->bl_ctx, BL_CONTEXT_FLUSH_NO_FLAGS);

    // Assumes force-redraw logic was fully handled above
    st_buffer->force_redraw = false;
}

static inline void
arm_selection_surface_frame_callback(
    struct scran_output *st_output,
    bool commit_if_armed // used as template specialization (if passing a literal)
) {
    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;

    if (!selection_surface->awaiting_frame_callback && !selection_surface->disable_reason_mask) {
        wl_callback_add_listener(
            wl_surface_frame(selection_surface->surface.wl_surface),
            &selection_surface_frame_callback_listener,
            st_output
        );
        selection_surface->awaiting_frame_callback = true;

        if (commit_if_armed) {
            wl_surface_commit(selection_surface->surface.wl_surface);
        }
    }
}

void
request_selection_surface_frame_callback(
    struct scran_output *st_output
) {
    arm_selection_surface_frame_callback(st_output, true);
}


// Draws the initial state of the buffers and commits.
//
// Caller is responsible for making sure buffer, surface etc. is valid (e.g.
// not busy).
//
// st_output.selection_surface.initial_box initialization must also happen
// prior to calling this function.
void
init_selection_surface_content(
    struct scran_output *st_output
) {
    DEBUG("  init_selection_surface_content()\n");

    struct scran_output_selectionSurface *selection_surface = &st_output->selection_surface;
    struct BLBoxI                         initial_box       =  st_output->initial_selection;

    const bool no_initial_selection = blboxi_are_equal(initial_box, SCRAN_INITIAL_SELECTION_NONE);

    if (no_initial_selection) {
        initial_box = get_selection_surface_pre_selection_box(st_output);
        selection_set_box_px(&st_output->selection_ctx, initial_box);

        selection_surface_set_theme(st_output, SURFACE_THEME_PRE_SELECTION);
    } else {
        // This must be set prior to set_selection_initialized()
        selection_set_box_px(&st_output->selection_ctx, initial_box);
        // These are usually called at "runtime"/main-loop-time, so call these
        // AFTER init_postmem__selection(), to ensure all relevant runtime
        // state has been set up.
        // ALSO make sure it's called somewhere that the freezeframe init path
        // (and potential future alternate init paths) will reach.
        selection_surface_set_theme(st_output, SURFACE_THEME_DEFAULT);
        selection_set_initialized(st_output);
    }

    for (int i = 0; i < SELECTION_SURFACE_BUF_COUNT; ++i) {
        struct scran_output_selectionSurface_buffer *st_buffer = &selection_surface->double_buffer[i];
        // Initialized as busy; reset them now.
        //   See init_premem__selection() for more info
        assert(st_buffer->scran_wl_buffer.busy == true);
        st_buffer->scran_wl_buffer.busy = false;
        // force-redraw, since collected ui state equal to zero-initialized
        // cached state does not necessarily imply nothing should be drawn
        st_buffer->force_redraw = true;
        draw_selection_and_damage_buffer(
            selection_surface,
            st_buffer,
            &st_output->selection_ctx,
            initial_box
        );
    }

    struct scran_output_selectionSurface_buffer *initial_buffer = &selection_surface->double_buffer[0];
    initial_buffer->scran_wl_buffer.busy = true;
    wl_surface_attach(
        selection_surface->surface.wl_surface, initial_buffer->scran_wl_buffer.wl_buffer, 0, 0
    );
    wl_surface_damage_buffer(
        selection_surface->surface.wl_surface,
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer
    );

    arm_selection_surface_frame_callback(st_output, false);
    wl_surface_commit(selection_surface->surface.wl_surface);
}
