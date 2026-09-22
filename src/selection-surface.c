#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <uchar.h>

#include <blend2d/blend2d.h>

#include "cursor.h"
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


static inline bool
on_greeting_screen(struct scran_output *output) {
    return selection_is_none(&output->selection_ctx);
}

static inline struct atlas_text_metrics
get_item_spacing(const struct atlas *atlas) {
    return (struct atlas_text_metrics) {
        .advance.x = round(3 * atlas->space_glyph_advance.x),
    };
}

struct selection_border {
    BLBoxI inner;
    BLBoxI outer;
};

// We trunc/ceil like this to make sure that fractionally scaled displays
// will not be able to bleed our capture border into the captured frame,
// not matter how they do their rounding/down-/upscaling.
// This does make our frame not always pixel-perfect with fractional scaling,
// but should not affect non-scaled displays.
static inline struct selection_border
get_selection_border(
    BLBoxI selection,
    double normalized_scale_factor
) {
    const BLBoxI inner_edge = {
        .x0 = trunc(trunc(selection.x0 / normalized_scale_factor) * normalized_scale_factor),
        .y0 = trunc(trunc(selection.y0 / normalized_scale_factor) * normalized_scale_factor),
        .x1 = ceil( ceil( selection.x1 / normalized_scale_factor) * normalized_scale_factor),
        .y1 = ceil( ceil( selection.y1 / normalized_scale_factor) * normalized_scale_factor),
    };

    return (struct selection_border) {
        .inner = inner_edge,
        .outer = blboxi_get_inflated(inner_edge, SCRAN_SELECTION_BORDER_THICKNESS_PX),
    };
}

// Sets up the context and bl_path for filling the background dim:
// everything within bounds, minus the hole for the selection (border included).
static inline void
prepare_background_fill(
    struct scran_output *output,
    struct scran_output_selectionSurface_buffer *st_buffer,
    const struct selection_border *border,
    const BLBoxI *surface_bounds
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    bl_context_set_comp_op(&st_buffer->bl_ctx, BL_COMP_OP_SRC_COPY);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, UI_COLOR_BG_DIM);
    bl_context_set_fill_rule(&st_buffer->bl_ctx, BL_FILL_RULE_EVEN_ODD);

    bl_path_add_box_i(&selection_surface->bl_path, surface_bounds, BL_GEOMETRY_DIRECTION_NONE);
    if (!on_greeting_screen(output)) { // TODO: likely()
        bl_path_add_box_i(&selection_surface->bl_path, &border->outer, BL_GEOMETRY_DIRECTION_NONE);
    }
}

// Sets up the context and bl_path for filling the selection border.
static inline void
prepare_selection_border_fill(
    struct scran_output_selectionSurface *selection_surface,
    struct scran_output_selectionSurface_buffer *st_buffer,
    const struct selection_border *border
) {
    bl_context_set_comp_op(&st_buffer->bl_ctx, BL_COMP_OP_SRC_COPY);
    bl_context_set_fill_style_rgba32(&st_buffer->bl_ctx, selection_surface->border_color);
    bl_context_set_fill_rule(&st_buffer->bl_ctx, BL_FILL_RULE_EVEN_ODD);

    bl_path_add_box_i(&selection_surface->bl_path, &border->inner,  BL_GEOMETRY_DIRECTION_NONE);
    bl_path_add_box_i(&selection_surface->bl_path, &border->outer, BL_GEOMETRY_DIRECTION_NONE);
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
    BLBoxI selection,
    const struct selection_border *border,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    prepare_selection_border_fill(selection_surface, st_buffer, border);

    for (int i = 0; i < n_damage_regions; ++i) {
        draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

    bl_path_clear(&selection_surface->bl_path);

    st_buffer->box_currently_drawn = selection;
}

static inline void
draw_and_damage_background(
    struct scran_output *output,
    struct scran_output_selectionSurface_buffer *st_buffer,
    const BLBoxI *surface_bounds,
    const struct selection_border *border,
    const BLRectI *damage_regions_wayland,
    const BLRectI *damage_regions_buffer,
    uint8_t n_damage_regions // shared between 'damage_regions_wayland' and 'damage_regions_buffer'
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    prepare_background_fill(output, st_buffer, border, surface_bounds);

    for (int i = 0; i < n_damage_regions; ++i) {
        draw_and_damage_region(selection_surface, st_buffer, damage_regions_wayland[i], damage_regions_buffer[i]);
    }

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

// TODO:
//   This currently also rewrites whatever was below the cleared items, i.e.
//   background and selection border.
//
//   Would maybe be better to rework the entire compositing pipeline to use a
//   scene-based approach where we calculate every ui/bg/selection dirty-rect
//   first, clear them all, and then just run them all through the scene
//   renderer.
static void
clear_old_ui_item(
    struct scran_output *output,
    struct scran_output_selectionSurface_buffer *st_buffer,
    const struct selection_border *border,
    const struct ui_item_geometry *geometry
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;
    const BLRectI text_rect = geometry_to_surface_rect_px(selection_surface, geometry);

    if (text_rect.w <= 0 || text_rect.h <= 0) {
        return;
    }

    const BLBoxI surface_bounds = {
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer,
    };

    bl_context_clip_to_rect_i(&st_buffer->bl_ctx, &text_rect);
    bl_context_clear_all(&st_buffer->bl_ctx);

    // Redraw background dim
    prepare_background_fill(output, st_buffer, border, &surface_bounds);
    bl_context_fill_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &selection_surface->bl_path);
    bl_path_clear(&selection_surface->bl_path);

    // Redraw selection border
    if (!on_greeting_screen(output)) {
        prepare_selection_border_fill(selection_surface, st_buffer, border);
        bl_context_fill_path_d(&st_buffer->bl_ctx, &SURFACE_BLCONTEXT_ORIGIN, &selection_surface->bl_path);
        bl_path_clear(&selection_surface->bl_path);
    }

    bl_context_restore_clipping(&st_buffer->bl_ctx);
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
    const struct selection_border *border,
    int surface_width_px,
    const struct atlas_text_metrics *textline_metrics,
    int height_px,
    bool ui_inside_selection,
    enum scran_horizontal_alignment alignment,
    enum scran_vertical_placement placement
) {
    const int advance_px = atlas_metrics_pen_x_px(textline_metrics);

    BLBoxI anchor = ui_inside_selection
        ? (BLBoxI){
            // HACK: The UI items are too close to the border without this, but only on
            // the X-axis. There's probably a nicer way to do this...
            .x0 = border->inner.x0 + 2,
            .y0 = border->inner.y0,
            .x1 = border->inner.x1 - 2,
            .y1 = border->inner.y1,
        }
        : border->outer;

    int origin_x = alignment == SCRAN_ALIGN_LEFT
        ? anchor.x0
        : anchor.x1 - advance_px;

    if (origin_x < anchor.x0) {
        origin_x = anchor.x0;
    }
    if (origin_x + advance_px > surface_width_px && advance_px <= surface_width_px) {
        origin_x = surface_width_px - advance_px;
    }

    const int origin_y = placement == SCRAN_PLACE_ABOVE
        ? anchor.y0 - (ui_inside_selection ? 0 : height_px)
        : anchor.y1 - (ui_inside_selection ? height_px : 0);

    if (!ui_inside_selection) {
        assert( // Sanity-assert: "outside" items are outside the capture area
            origin_y + height_px <= border->outer.y0
            || origin_y >= border->outer.y1
        );
    }

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

    bl_context_set_comp_op(bl_ctx, BL_COMP_OP_SRC_OVER);

    for (size_t i = 0; i < n_items; ++i) {
        if (i > 0) {
            const struct atlas_text_metrics spacing = get_item_spacing(atlas);
            atlas_append_text_metrics(&line_metrics, &spacing);
        }

        const BLPointI item_origin = {
            .x = origin->x + atlas_metrics_pen_x_px(&line_metrics),
            .y = origin->y,
        };
        const struct atlas_text_metrics item_metrics = atlas_blit_string(atlas, bl_ctx, &item_origin, &blit_data[i]);
        atlas_append_text_metrics(&line_metrics, &item_metrics);
    }

    return line_metrics;
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
shared_contents_equal(const struct ui_shared_content *a, const struct ui_shared_content *b) {
    return a->ui_inside_selection == b->ui_inside_selection;
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

// For UI elements that don't store their blit data in their description struct
struct ui_render_data {
    struct atlas_blit_data greeting[UI_GREETING_N_ITEMS];

    char16_t selection_size[SELECTION_SIZE_STRLEN];
    char16_t timer[TIMER_STRLEN];
    struct atlas_blit_data statusline[UI_STATUSLINE_N_ITEMS];
};

static inline struct ui_greeting_content
collect_greeting_content(struct scran_output *output) {
    return (struct ui_greeting_content){
        .visible = on_greeting_screen(output),
    };
}

static inline void
make_greeting_description(
    struct scran_output *output,
    const struct selection_border *border,
    struct ui_greeting_description *description,
    struct ui_render_data *render_data
) {
    const struct ui_greeting_content content = collect_greeting_content(output);

    *description = (struct ui_greeting_description){
        .content = content,
    };

    render_data->greeting[UI_GREETING_ITEM_GREETING] = (struct atlas_blit_data){
        .string = content.visible ? UI_STRING(g_ui_strings.greeting) : UI_STRING(g_ui_strings.empty),
        .color = UI_COLOR_TEXT_DEFAULT,
    };

    const int item_height_px = atlas_font_height_px(&output->selection_surface.atlas);

    if (content.visible) {
        const struct atlas_text_metrics metrics = measure_ui_line(
            &output->selection_surface.atlas, render_data->greeting, ARRAY_LENGTH(render_data->greeting)
        );
        description->geometry = get_ui_item_geometry(
            border,
            output->selection_surface.surface.width_px_buffer,
            &metrics,
            item_height_px,
            false, // The greeting is never displayed while we have a selection
            SCRAN_ALIGN_LEFT,
            SCRAN_PLACE_ABOVE
        );
        description->geometry.pen_origin.y -= item_height_px;
    }
}

static inline struct ui_keymap_content
collect_keymap_content(
    struct scran_output *output
) {
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

static void
make_keymap_description(
    struct scran_output *output,
    const struct selection_border *border,
    struct ui_keymap_description *description
) {
    const struct ui_keymap_content content = collect_keymap_content(output);

    const struct atlas_text_metrics metrics = measure_ui_line(&output->selection_surface.atlas, content.blit_data, ARRAY_LENGTH(content.blit_data));
    const struct ui_item_geometry geometry = get_ui_item_geometry(
        border,
        output->selection_surface.surface.width_px_buffer,
        &metrics,
        atlas_font_height_px(&output->selection_surface.atlas),
        output->selection_surface.ui_inside_selection,
        SCRAN_ALIGN_LEFT,
        SCRAN_PLACE_BELOW
    );

    *description = (struct ui_keymap_description){
        .geometry = geometry,
        .content = content,
    };
}

static inline int
get_video_timer_seconds(struct scran_output *output, int64_t now_ns) {
    if (!capture_video_is_live(output)) {
        return 0;
    }

    const int64_t elapsed_nsec = now_ns - output->capture.video_presentation_time_nsec_start;
    if (elapsed_nsec <= 0) {
        return 0;
    }

    const int64_t elapsed_seconds = elapsed_nsec / NSEC_PER_SEC;

    return MIN(elapsed_seconds, INT_MAX);
}

static inline struct ui_statusline_content
collect_statusline_content(
    struct scran_output *output,
    BLBoxI selection,
    int64_t now_ns
) {
    const BLBoxI effective_selection =
        on_greeting_screen(output)
        ? get_fullscreen_selection_box(output)
        : selection;
    const BLPointI effective_selection_dimensions = blboxi_get_dimensions(effective_selection);

    return (struct ui_statusline_content){
        .selection_width = effective_selection_dimensions.x,
        .selection_height = effective_selection_dimensions.y,
        .timer_seconds = get_video_timer_seconds(output, now_ns),
    };
}

static inline struct ui_shared_content
collect_shared_content(struct scran_output *output) {
    return (struct ui_shared_content){
        .ui_inside_selection = output->selection_surface.ui_inside_selection,
    };
}

bool
ui_contents_equal(
    struct scran_output *output,
    const BLBoxI *selection,
    int64_t now_ns
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    struct ui_shared_content     shared_content     = collect_shared_content(output);
    struct ui_statusline_content statusline_content = collect_statusline_content(output, *selection, now_ns);
    struct ui_keymap_content     keymap_content     = collect_keymap_content(output);
    struct ui_greeting_content   greeting_content   = collect_greeting_content(output);

    return
        shared_contents_equal(&selection_surface->ui_last_committed.shared_contents, &shared_content)
        && greeting_content_equal(&selection_surface->ui_last_committed.greeting.content, &greeting_content)
        && statusline_content_equal(&selection_surface->ui_last_committed.statusline.content, &statusline_content)
        && keymap_content_equal(&selection_surface->ui_last_committed.keymap.content, &keymap_content);
}

static void
make_statusline_description(
    struct scran_output *output,
    BLBoxI selection,
    const struct selection_border *border,
    int64_t now_ns,
    struct ui_statusline_description *description,
    struct ui_render_data *render_data
) {
    const struct ui_statusline_content content = collect_statusline_content(output, selection, now_ns);

    get_selection_size_string(render_data->selection_size, &content);
    get_timer_string(render_data->timer, content.timer_seconds);

    render_data->statusline[UI_STATUSLINE_ITEM_SELECTION_SIZE] = (struct atlas_blit_data){
        .string = { .str = render_data->selection_size, .strlen = ARRAY_LENGTH(render_data->selection_size) },
        .color =  UI_COLOR_TEXT_DEFAULT,
    };

    render_data->statusline[UI_STATUSLINE_ITEM_TIMER] = (struct atlas_blit_data){
        .string = { .str = render_data->timer, .strlen = ARRAY_LENGTH(render_data->timer) },
        .color =  UI_COLOR_TEXT_DEFAULT,
    };

    const struct atlas_text_metrics metrics = measure_ui_line(&output->selection_surface.atlas, render_data->statusline, ARRAY_LENGTH(render_data->statusline));

    const struct ui_item_geometry geometry = get_ui_item_geometry(
        border,
        output->selection_surface.surface.width_px_buffer,
        &metrics,
        atlas_font_height_px(&output->selection_surface.atlas),
        output->selection_surface.ui_inside_selection,
        SCRAN_ALIGN_RIGHT,
        SCRAN_PLACE_ABOVE
    );

    *description = (struct ui_statusline_description){
        .geometry = geometry,
        .content = content,
    };
}

static void
make_ui_description(
    struct scran_output *output,
    BLBoxI selection,
    const struct selection_border *border,
    int64_t now_ns,
    struct ui_description *description,
    struct ui_render_data *render_data
) {
    description->shared_contents = collect_shared_content(output);

    make_greeting_description(output, border, &description->greeting, render_data);
    make_keymap_description(output, border, &description->keymap);
    make_statusline_description(output, selection, border, now_ns, &description->statusline, render_data);
}

static void
draw_and_damage_ui(
    struct scran_output *output,
    struct scran_output_selectionSurface_buffer *st_buffer,
    BLBoxI selection,
    const struct selection_border *border
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    struct ui_description new_ui;
    struct ui_render_data render_data;
    make_ui_description(output, selection, border, capture_clock_gettime_nsec(), &new_ui, &render_data);


    struct ui_item_render_plan {
        bool redraw_buffer;
        bool damage_surface;
        const struct ui_item_geometry *buffer_geometry;
        const struct ui_item_geometry *committed_geometry;
        const struct ui_item_geometry *new_geometry;
        const struct atlas_blit_data *blit_data;
        size_t n_blit_items;
    };

    struct ui_item_render_plan render_plan[] = {
        { // Greeting
            .redraw_buffer =
                st_buffer->force_redraw
                || !greeting_description_equal(&st_buffer->ui.greeting, &new_ui.greeting),
            .damage_surface = !greeting_description_equal(
                &selection_surface->ui_last_committed.greeting,
                &new_ui.greeting
            ),
            .buffer_geometry = &st_buffer->ui.greeting.geometry,
            .committed_geometry = &selection_surface->ui_last_committed.greeting.geometry,
            .new_geometry = &new_ui.greeting.geometry,
            .blit_data = render_data.greeting,
            .n_blit_items = ARRAY_LENGTH(render_data.greeting),
        },
        { // Status line
            .redraw_buffer =
                st_buffer->force_redraw
                || !statusline_description_equal(&st_buffer->ui.statusline, &new_ui.statusline),
            .damage_surface = !statusline_description_equal(
                &selection_surface->ui_last_committed.statusline,
                &new_ui.statusline
            ),
            .buffer_geometry = &st_buffer->ui.statusline.geometry,
            .committed_geometry = &selection_surface->ui_last_committed.statusline.geometry,
            .new_geometry = &new_ui.statusline.geometry,
            .blit_data = render_data.statusline,
            .n_blit_items = ARRAY_LENGTH(render_data.statusline),
        },
        { // Keymap
            .redraw_buffer =
                st_buffer->force_redraw
                || !keymap_description_equal(&st_buffer->ui.keymap, &new_ui.keymap),
            .damage_surface = !keymap_description_equal(
                &selection_surface->ui_last_committed.keymap,
                &new_ui.keymap
            ),
            .buffer_geometry = &st_buffer->ui.keymap.geometry,
            .committed_geometry = &selection_surface->ui_last_committed.keymap.geometry,
            .new_geometry = &new_ui.keymap.geometry,
            .blit_data = new_ui.keymap.content.blit_data,
            .n_blit_items = ARRAY_LENGTH(new_ui.keymap.content.blit_data),
        },
    };

    // Clear previous items
    //   We clear all stale items before drawing any new items,
    //   so later items don't clear out earlier ones
    //
    //   `force_redraw` clears the entire buffer before arriving here,
    //   so we don't need to clear it again.
    if (!st_buffer->force_redraw) {
        for (size_t i = 0; i < ARRAY_LENGTH(render_plan); ++i) {
            const struct ui_item_render_plan *item = &render_plan[i];
            if (item->redraw_buffer) {
                clear_old_ui_item(output, st_buffer, border, item->buffer_geometry);
            }
        }
    }

    // Redraw buffer contents
    for (size_t i = 0; i < ARRAY_LENGTH(render_plan); ++i) {
        const struct ui_item_render_plan *item = &render_plan[i];
        if (!item->redraw_buffer) {
            continue;
        }

        // We must control the bounds ourselves so we can prevent text from
        // appearing inside the capture area.
        //     TODO: Vertical bbox metrics aren't actually implemented yet,
        //     at time of writing. When they are, this to_surface_rect function
        //     should be updated accordingly.
        const BLRectI clip_rect = geometry_to_surface_rect_px(selection_surface, item->new_geometry);
        bl_context_clip_to_rect_i(&st_buffer->bl_ctx, &clip_rect);

        const struct atlas_text_metrics _metrics = blit_ui_line(
            &selection_surface->atlas,
            &st_buffer->bl_ctx,
            &item->new_geometry->pen_origin,
            item->blit_data,
            item->n_blit_items
        );
        assert(atlas_metrics_equal(&_metrics, &item->new_geometry->text_metrics));
        (void)_metrics;

        bl_context_restore_clipping(&st_buffer->bl_ctx);
    }

    // Submit Wayland damage
    for (size_t i = 0; i < ARRAY_LENGTH(render_plan); ++i) {
        const struct ui_item_render_plan *item = &render_plan[i];
        if (item->damage_surface) {
            damage_ui_item(selection_surface, item->committed_geometry);
            damage_ui_item(selection_surface, item->new_geometry);
        }
    }

    // Update cursor
    {
        // Keep the ui_inside toggle tooltip visible while
        //   1. the UI is inside the capture area, or
        //   2. the UI is clipping against the edge of the surface.
        //   XXX TODO: Rework fullscreen capture pipeline to allow showing UI
        //   inside during fullscreen captures.
        bool ui_is_clipping = false;
        const bool ui_inside = new_ui.shared_contents.ui_inside_selection;

        if (!ui_inside) {
            const BLBoxI surface_bounds = {
                0, 0,
                selection_surface->surface.width_px_buffer,
                selection_surface->surface.height_px_buffer,
            };

            for (size_t i = 0; i < ARRAY_LENGTH(render_plan); ++i) {
                const struct ui_item_render_plan *item = &render_plan[i];
                const BLBoxI item_bounds = blrecti_to_blboxi(
                    geometry_to_surface_rect_px(selection_surface, item->new_geometry)
                );

                if (!blboxi_contains(surface_bounds, item_bounds)) {
                    ui_is_clipping = true;
                    break;
                }
            }
        }

        const enum scran_cursor_tooltip tooltip =
            (ui_inside || ui_is_clipping)
            ? SCRAN_CURSOR_TOOLTIP_FLIP_UI
            : SCRAN_CURSOR_TOOLTIP_NONE;

        if (output->cursor.tooltip != tooltip) {
            cursor_set_tooltip(output, tooltip);
        }
    }

    st_buffer->ui = new_ui;
    selection_surface->ui_last_committed = new_ui;
}

void
draw_selection_and_damage_buffer(
    struct scran_output *output,
    struct scran_output_selectionSurface_buffer *st_buffer,
    struct BLBoxI desired_selection
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    if (g_state.options.hide_ui_level >= SCRAN_OPT_HIDE_UI_EVERYTHING) {
        // XXX: Slightly spaghetti, but required for updating the capture area.
        st_buffer->box_currently_drawn = desired_selection;
        return;
    }

    // TODO: Assert bl_ctx has already begun

    // What the compositor has to overwrite:
    const struct BLBoxI committed_selection = selection_surface->committed_selection;
    // What we have to overwrite:
    const struct BLBoxI buffer_selection = st_buffer->box_currently_drawn;

    assert(!blboxi_is_inverted(desired_selection));
    assert(!blboxi_is_inverted(committed_selection));
    // TODO: Assert box_bounds fully surrounds box_to_draw

    const struct BLBoxI surface_bounds = {
        0, 0,
        selection_surface->surface.width_px_buffer,
        selection_surface->surface.height_px_buffer,
    };

    const double scale = selection_surface->surface.final_scale_factor_normalized;

    const struct selection_border desired_border   = get_selection_border(desired_selection, scale);
    const struct selection_border committed_border = get_selection_border(committed_selection, scale);
    const struct selection_border buffer_border    = get_selection_border(buffer_selection, scale);

    bool selection_changed =
        !blboxi_are_equal(desired_selection, st_buffer->box_currently_drawn)
        || !blboxi_are_equal(desired_selection, selection_surface->committed_selection);

    // Draw background dim
    if (selection_changed || st_buffer->force_redraw) {
        int n_damage_regions;
        BLRectI damage_regions_wayland[8];
        BLRectI damage_regions_buffer[8];

        // TODO: Just do redraw/damage directly whenever we need to redraw, rather than
        // needing to branch within this function?
        if (st_buffer->force_redraw) {
            const BLRectI damage_region_everything = blboxi_to_blrecti(surface_bounds);
            damage_regions_wayland[0] = damage_region_everything;
            damage_regions_buffer[0] = damage_region_everything;
            n_damage_regions = 1;
        } else {
            static const int i_background_diffs = 0;
            blboxi_get_symmetric_difference_as_4_rects(committed_border.outer, desired_border.outer, damage_regions_wayland + i_background_diffs);
            blboxi_get_symmetric_difference_as_4_rects(buffer_border.outer,    desired_border.outer, damage_regions_buffer  + i_background_diffs);
            static const int i_old_border_diffs = 4;
            blboxi_get_symmetric_difference_as_4_rects(committed_border.outer, committed_border.inner, damage_regions_wayland + i_old_border_diffs);
            blboxi_get_symmetric_difference_as_4_rects(buffer_border.outer,    buffer_border.inner,    damage_regions_buffer  + i_old_border_diffs);
            n_damage_regions = 8;
        }

        draw_and_damage_background(
            output,
            st_buffer,
            &surface_bounds,
            &desired_border,
            damage_regions_wayland,
            damage_regions_buffer,
            n_damage_regions
        );
    }

    if (g_state.options.hide_ui_level < SCRAN_OPT_HIDE_UI_ITEMS) {
        // UI items must be drawn after/on top of the background.
        draw_and_damage_ui(output, st_buffer, desired_selection, &desired_border);
    }

    // Draw selection border
    if (selection_changed || st_buffer->force_redraw) {
        if (on_greeting_screen(output)) { // TODO: unlikely()
            st_buffer->box_currently_drawn = desired_selection;
        } else {
            BLRectI damage_regions[4];
            blboxi_get_symmetric_difference_as_4_rects(desired_border.outer, desired_border.inner, damage_regions);
            draw_and_damage_selection_border(
                selection_surface,
                st_buffer,
                desired_selection,
                &desired_border,
                damage_regions,
                damage_regions,
                4
            );
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
    struct scran_output *output,
    bool commit_if_armed // used as template specialization (if passing a literal)
) {
    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;

    if (!selection_surface->awaiting_frame_callback && !selection_surface->disable_reason_mask) {
        wl_callback_add_listener(
            wl_surface_frame(selection_surface->surface.wl_surface),
            &selection_surface_frame_callback_listener,
            output
        );
        selection_surface->awaiting_frame_callback = true;

        if (commit_if_armed) {
            wl_surface_commit(selection_surface->surface.wl_surface);
        }
    }
}

void
request_selection_surface_frame_callback(struct scran_output *output)
{
    arm_selection_surface_frame_callback(output, true);
}


// Draws the initial state of the buffers and commits.
//
// Caller is responsible for making sure buffer, surface etc. is valid (e.g.
// not busy).
//
// output.selection_surface.initial_box initialization must also happen
// prior to calling this function.
void
init_selection_surface_content(struct scran_output *output)
{
    DEBUG("  init_selection_surface_content()\n");

    struct scran_output_selectionSurface *selection_surface = &output->selection_surface;
    struct BLBoxI                         initial_box       =  output->initial_selection;

    const bool no_initial_selection = blboxi_are_equal(initial_box, SCRAN_INITIAL_SELECTION_NONE);

    if (no_initial_selection) {
        initial_box = get_selection_surface_pre_selection_box(output);
        selection_set_box_px(&output->selection_ctx, initial_box);
        // Alpha channel must not be ignored for inivisibility.
        assert(SURFACE_SHM_FORMAT_BL == BL_FORMAT_PRGB32);
        selection_surface_set_border_color(output, 0x00000000U);
    } else {
        // This must be set prior to set_selection_initialized()
        selection_set_box_px(&output->selection_ctx, initial_box);
        // These are usually called at "runtime"/main-loop-time, so call these
        // AFTER init_postmem__selection(), to ensure all relevant runtime
        // state has been set up.
        // ALSO make sure it's called somewhere that the freezeframe init path
        // (and potential future alternate init paths) will reach.
        selection_surface_set_border_color(output, UI_COLOR_SELECTION_DEFAULT);
        selection_set_initialized(output);
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
        draw_selection_and_damage_buffer(output, st_buffer, initial_box);
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

    arm_selection_surface_frame_callback(output, false);
    wl_surface_commit(selection_surface->surface.wl_surface);
    selection_surface->committed_selection = initial_box;
}
