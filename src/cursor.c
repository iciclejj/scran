#include <assert.h>
#include <math.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "atlas.h"
#include "scran-ui-text.h"
#include "viewporter.h"

#include "state.h"
#include "cursor.h"
#include "init.h"
#include "selection-surface.h"
#include "util/util.h"
#include "util/blend2d.h"
#include "print.h"


static const BLRgba32 m_cursor_colors[] = {
    [SCRAN_CURSOR_THEME_DEFAULT].value       = UI_COLOR_SELECTION_DEFAULT,
    [SCRAN_CURSOR_THEME_VIDEO_CAPTURE].value = UI_COLOR_VIDEO_CAPTURE,
};
static_assert(ARRAY_LENGTH(m_cursor_colors) == SCRAN_CURSOR_N_THEMES,
              "m_cursor_colors[] length must exactly cover all cursor themes.");

static const BLRgba32 m_cursor_outline_color = { 0xFF000000 };


bool
init_premem__cursor(struct scran_output *st_output)
{
    struct scran_cursor *cursor = &st_output->cursor;

    cursor->wl_surface = wl_compositor_create_surface(g_state.globals.compositor);
    cursor->viewport = wp_viewporter_get_viewport(g_state.globals.viewporter, cursor->wl_surface);

    for (int i = 0; i < SCRAN_CURSOR_N_THEMES; ++i) {
        for (int j = 0; j < SCRAN_CURSOR_N_TOOLTIPS; ++j) {
            bl_image_init(&cursor->buffers[i][j].bl_img);
        }
    }

    // TODO: Do something better than hardcoding initial scale?
    atlas_init(&cursor->atlas, 1);

    return true;
}

void
init_premem__cursor__destroy(struct scran_output *st_output)
{
    struct scran_cursor *cursor = &st_output->cursor;

    wp_viewport_destroy(cursor->viewport);
    wl_surface_destroy(cursor->wl_surface);
    atlas_destroy(&cursor->atlas);
}

bool
init_postmem__cursor(struct scran_output *st_output)
{
#ifndef NDEBUG
    for (int i = 0; i < SCRAN_CURSOR_N_THEMES; ++i) {
        for (int j = 0; j < SCRAN_CURSOR_N_TOOLTIPS; ++j) {
            assert(st_output->cursor.buffers[i][j].scran_wl_buffer.data != NULL);
        }
    }
#endif

    return cursor_reinit(st_output);
}

void
init_postmem__cursor__destroy(struct scran_output *st_output)
{
    for (int i = 0; i < SCRAN_CURSOR_N_THEMES; ++i) {
        for (int j = 0; j < SCRAN_CURSOR_N_TOOLTIPS; ++j) {
            bl_image_destroy(&st_output->cursor.buffers[i][j].bl_img);
        }
    }
}

static inline void
draw_cursor(
    struct scran_cursor_buffer *buffer,
    BLContextCore *bl_ctx,
    BLPointI origin,
    int cursor_size_px,
    BLRgba32 color
) {
    int stroke_width_px = ceil(cursor_size_px * 0.1);
    // Make sure crosshair is always centered on the hotspot, whether even or odd width.
    if ((stroke_width_px & 0b1) != (cursor_size_px & 0b1)) {
        stroke_width_px += 1;
    }

    const int outline_width_px         = MAX(1, round((double)cursor_size_px / SCRAN_CURSOR_SIZE));
    const int outlined_stroke_width_px = stroke_width_px + 2 * outline_width_px;

    const int stroke_rect_xy_px          = (cursor_size_px - stroke_width_px) / 2;
    const int outlined_stroke_rect_xy_px = (cursor_size_px - outlined_stroke_width_px) / 2;

    // Draw outline-width layer first
    bl_context_set_fill_style_rgba32(bl_ctx, m_cursor_outline_color.value);
    bl_context_fill_rect_i(
        bl_ctx,
        &(BLRectI){
            .x = origin.x + 0,
            .y = origin.y + outlined_stroke_rect_xy_px,
            .w = cursor_size_px,
            .h = outlined_stroke_width_px,
        }
    );
    bl_context_fill_rect_i(
        bl_ctx,
        &(BLRectI){
            .x = origin.x + outlined_stroke_rect_xy_px,
            .y = origin.y + 0,
            .w = outlined_stroke_width_px,
            .h = cursor_size_px,
        }
    );

    // Then draw smaller main cursor body over it
    bl_context_set_fill_style_rgba32(bl_ctx, color.value);
    bl_context_fill_rect_i(
        bl_ctx,
        &(BLRectI){
            .x = origin.x + outline_width_px,
            .y = origin.y + stroke_rect_xy_px,
            .w = cursor_size_px - 2 * outline_width_px,
            .h = stroke_width_px,
        }
    );
    bl_context_fill_rect_i(
        bl_ctx,
        &(BLRectI){
            .x = origin.x + stroke_rect_xy_px,
            .y = origin.y + outline_width_px,
            .w = stroke_width_px,
            .h = cursor_size_px - 2 * outline_width_px,
        }
    );
}

// Returns tooltip dimensions
static inline BLPointI
draw_tooltip(
    struct scran_output *output,
    struct scran_cursor_buffer *buffer,
    BLContextCore *bl_ctx,
    BLPointI origin,
    int cursor_size_px,
    enum scran_cursor_tooltip tooltip
) {
    // Draw tooltip
    struct atlas_blit_data tooltip_blit_data;
    struct atlas *atlas = &output->cursor.atlas;

    switch (tooltip) {
        case SCRAN_CURSOR_TOOLTIP_NONE:
            return (BLPointI){ 0, 0 };
        case SCRAN_CURSOR_TOOLTIP_FLIP_UI:
            tooltip_blit_data = (struct atlas_blit_data){
                UI_STRING(g_ui_strings.cursor_tooltip_flipped_ui),
                m_cursor_colors[SCRAN_CURSOR_THEME_DEFAULT].value,
            };
            break;
        case SCRAN_CURSOR_N_TOOLTIPS:
            eprintf("Error: Unexpected cursor tooltip\n");
            exit(EXIT_FAILURE);
    }

    // TODO: Clip to tooltip rect, for future fonts that might extend behind the pen origin?
    atlas_blit_string(atlas, bl_ctx, &origin, &tooltip_blit_data);

    struct atlas_text_metrics metrics = atlas_get_text_metrics_px(atlas, &tooltip_blit_data.string);
    return (BLPointI){
        .x = MAX(ceil(metrics.advance.x), metrics.bbox.x1),
        .y = atlas_font_height_px(atlas), // TODO: use Y-coordinates once implemented
    };
}

static inline float
get_cursor_scale(struct scran_output *st_output) {
    return MIN(ceil(st_output->selection_surface.surface.final_scale_factor_normalized), SCRAN_CURSOR_MAX_SCALE);
}

// XXX(Hyprland #15870)
static inline int
get_integer_cursor_scale(struct scran_output *st_output) {
    float scale = get_cursor_scale(st_output);
    return (scale <= 1) ? 1
           : (scale <= 2) ? 2 : 4;
}

static void
update_buffer(struct scran_output *output)
{
    struct scran_cursor *cursor = &output->cursor;

    enum scran_cursor_theme   theme   = cursor->theme;
    enum scran_cursor_tooltip tooltip = cursor->tooltip;

    struct wl_buffer *wl_buffer = cursor->buffers[theme][tooltip].scran_wl_buffer.wl_buffer;
    assert(wl_buffer != NULL);

    // cursor image is reset on every pointer::leave/enter
    bool have_pointer_focus = g_state.seat.pointer_ctx.focused_selection_surface == &output->selection_surface;
    if (!have_pointer_focus) {
        return;
    }

    const BLRectI viewport_source_px = cursor->viewport_source_px[theme][tooltip];
    const BLRectI viewport_source_scaled = cursor->viewport_source_scaled[theme][tooltip];

    // XXX(Hyprland #15870):
    //   Don't use cursor viewport *destination* until fixed, since it
    //   alters the required hotspot coordinates.
    //
    //   Viewport source rect is interpreted *after* set_buffer_scale,
    //   so until we switch back to wp_viewport_set_destination, we should
    //   just use the unscaled size.
    wp_viewport_set_source(
        cursor->viewport,
        wl_fixed_from_int(viewport_source_scaled.x),
        wl_fixed_from_int(viewport_source_scaled.y),
        wl_fixed_from_int(viewport_source_scaled.w),
        wl_fixed_from_int(viewport_source_scaled.h)
    );

    wl_pointer_set_cursor(
        g_state.seat.wl_pointer,
        g_state.seat.pointer_ctx.last_enter_serial,
        cursor->wl_surface,
        SCRAN_CURSOR_SIZE / 2,
        SCRAN_CURSOR_SIZE / 2
    );
    wl_surface_attach(cursor->wl_surface, wl_buffer, 0, 0);
    wl_surface_damage_buffer(
        cursor->wl_surface,
        viewport_source_px.x,
        viewport_source_px.y,
        viewport_source_px.w,
        viewport_source_px.h
    );
    wl_surface_commit(cursor->wl_surface);
}

// Store the states, since pointer::leave/enter events need the cursor to be re-set.
// Also for cursor_reinit().
static inline void
set_theme_state(struct scran_output *output, enum scran_cursor_theme theme) {
    output->cursor.theme = theme;
}
static inline void
set_tooltip_state(struct scran_output *output, enum scran_cursor_tooltip tooltip) {
    output->cursor.tooltip = tooltip;
}

void
cursor_set_theme(
    struct scran_output *output,
    enum scran_cursor_theme theme
) {
    set_theme_state(output, theme);
    // TODO: Check if buffer needs update in the main loop UI update checker instead?
    update_buffer(output);
}

void
cursor_set_tooltip(
    struct scran_output *output,
    enum scran_cursor_tooltip tooltip
) {
    set_tooltip_state(output, tooltip);
    // TODO: Check if buffer needs update in the main loop UI update checker instead?
    update_buffer(output);
}

bool
cursor_reinit(struct scran_output *output)
{
    struct scran_cursor *cursor = &output->cursor;
    // XXX(Hyprland #15870):
    //   Can't set cursor viewport, so just use an integer scale and
    //   set_buffer_scale instead.
    int scale = get_integer_cursor_scale(output);
    if (scale == 0) {
        eprintf("Warning: reinit_cursor() got scale=0; using scale=1\n");
        scale = 1;
    }
    wl_surface_set_buffer_scale(cursor->wl_surface, scale);
    atlas_reinit(&cursor->atlas, scale);

    // Clamp since we use compile-time buffer sizes
    int cursor_size_px = MAX(
        1,
        MIN(round(SCRAN_CURSOR_SIZE * scale), SCRAN_CURSOR_BUFFER_WIDTH_PX)
    );
    cursor->cursor_size_px = cursor_size_px;

    // Scale events can arrive before shared memory allocation is complete.
    bool buffers_initialized = (bool)cursor->buffers[0][0].scran_wl_buffer.data;
    if (!buffers_initialized) {
        return true;
    }

    for (int theme = 0; theme < SCRAN_CURSOR_N_THEMES; ++theme) {
        for (int tooltip = 0; tooltip < SCRAN_CURSOR_N_TOOLTIPS; ++tooltip) {
            struct scran_cursor_buffer *buffer = &cursor->buffers[theme][tooltip];

            // XXX(Hyprland #15870): Hyprland ignores cursor surface viewports. For now, just clear the entire buffer.
            // bl_context_clear_all(&bl_ctx);
            memset(
                buffer->scran_wl_buffer.data,
                0,
                get_framebuffer_size(SCRAN_CURSOR_BUFFER_WIDTH_PX, SCRAN_CURSOR_BUFFER_HEIGHT_PX, SURFACE_PIXEL_STRIDE)
            );

            assert(buffer->scran_wl_buffer.data);
            bl_image_create_from_data(
                &buffer->bl_img,
                SCRAN_CURSOR_BUFFER_WIDTH_PX,
                SCRAN_CURSOR_BUFFER_HEIGHT_PX,
                SURFACE_SHM_FORMAT_BL,
                buffer->scran_wl_buffer.data,
                SCRAN_CURSOR_BUFFER_WIDTH_PX * SURFACE_PIXEL_STRIDE,
                BL_DATA_ACCESS_RW,
                NULL,
                NULL
            );
            BLContextCore bl_ctx;
            bl_context_init_as(&bl_ctx, &buffer->bl_img, NULL);

            const BLPointI origin = { 0, 0 };
            BLPointI current_origin = origin;
            static const int item_spacing_px = 2;

            draw_cursor(buffer, &bl_ctx, current_origin, cursor_size_px, m_cursor_colors[theme]);
            current_origin.x += cursor_size_px + item_spacing_px;

            BLPointI tooltip_dimensions = draw_tooltip(output, buffer, &bl_ctx, current_origin, cursor_size_px, tooltip);
            current_origin.x += tooltip_dimensions.x + item_spacing_px;

            // TODO: make the item-spacing calculations cleaner.
            if (tooltip_dimensions.x > 0) {
                current_origin.x -= item_spacing_px;
            }

            bl_context_end(&bl_ctx);
            bl_context_destroy(&bl_ctx);

            BLRectI sprite_bbox = {
                .x = origin.x,
                .y = origin.y,
                .w = current_origin.x - origin.x,
                .h = MAX(cursor_size_px, tooltip_dimensions.y),
            };
            assert(
                blboxi_contains(
                    (BLBoxI){
                        .x0 = 0,
                        .y0 = 0,
                        .x1 = SCRAN_CURSOR_BUFFER_WIDTH_PX,
                        .y1 = SCRAN_CURSOR_BUFFER_HEIGHT_PX,
                    },
                    blrecti_to_blboxi(sprite_bbox)
                )
            );

            cursor->viewport_source_px[theme][tooltip] = sprite_bbox;
            cursor->viewport_source_scaled[theme][tooltip] = (BLRectI){
                .x = sprite_bbox.x / scale,
                .y = sprite_bbox.y / scale,
                .w = ceil(sprite_bbox.w / (double)scale),
                .h = ceil(sprite_bbox.h / (double)scale),
            };
        }
    }
    // XXX(Hyprland #15870): Damage everything, since we cleared everything.
    wl_surface_damage_buffer(
        cursor->wl_surface,
        0, 0,
        SCRAN_CURSOR_BUFFER_WIDTH_PX,
        SCRAN_CURSOR_BUFFER_HEIGHT_PX
    );

    set_theme_state(output, cursor->theme);
    set_tooltip_state(output, cursor->tooltip);
    update_buffer(output);

    return true;
}
