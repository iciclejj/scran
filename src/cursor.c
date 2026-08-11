#include <assert.h>
#include <math.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "viewporter.h"

#include "state.h"
#include "cursor.h"
#include "init.h"
#include "selection-surface.h"


extern struct scran g_state;


static const BLRgba32 m_cursor_colors[] = {
    [SCRAN_CURSOR_THEME_DEFAULT]       = SCRAN_SELECTION_BORDER_COLOR_DEFAULT,
    [SCRAN_CURSOR_THEME_VIDEO_CAPTURE] = SCRAN_SELECTION_BORDER_COLOR_VIDEO_CAPTURE,
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
        bl_image_init(&cursor->buffers[i].bl_img);
    }

    return true;
}

void
init_premem__cursor__destroy(struct scran_output *st_output)
{
    struct scran_cursor *cursor = &st_output->cursor;

    wp_viewport_destroy(cursor->viewport);
    wl_surface_destroy(cursor->wl_surface);
}

bool
init_postmem__cursor(struct scran_output *st_output)
{
    for (int i = 0; i < SCRAN_CURSOR_N_THEMES; ++i) {
        assert(st_output->cursor.buffers[i].scran_wl_buffer.data != NULL);
    }

    return cursor_reinit(st_output);
}

void
init_postmem__cursor__destroy(struct scran_output *st_output)
{
    for (int i = 0; i < SCRAN_CURSOR_N_THEMES; ++i) {
        bl_image_destroy(&st_output->cursor.buffers[i].bl_img);
    }
}

static inline void
draw_cursor(
    struct scran_cursor_buffer *buffer,
    int width_height_px,
    BLRgba32 color
) {
    assert(buffer->scran_wl_buffer.data);
    bl_image_create_from_data(
        &buffer->bl_img,
        width_height_px,
        width_height_px,
        SURFACE_SHM_FORMAT_BL,
        buffer->scran_wl_buffer.data,
        SCRAN_CURSOR_BUFFER_WIDTH_HEIGHT_PX * SURFACE_PIXEL_STRIDE,
        BL_DATA_ACCESS_RW,
        NULL,
        NULL
    );

    BLContextCore bl_ctx;
    bl_context_init(&bl_ctx);
    bl_context_begin(&bl_ctx, &buffer->bl_img, NULL);
    bl_context_clear_all(&bl_ctx);
    int stroke_width_px = ceil(width_height_px * 0.1);
    // Make sure crosshair is always centered on the hotspot, whether even or odd width.
    if ((stroke_width_px & 0b1) != (width_height_px & 0b1)) {
        stroke_width_px += 1;
    }

    const int outline_width_px         = MAX(1, round((double)width_height_px / SCRAN_CURSOR_WIDTH_HEIGHT));
    const int outlined_stroke_width_px = stroke_width_px + 2 * outline_width_px;

    const int stroke_rect_xy_px          = (width_height_px - stroke_width_px) / 2;
    const int outlined_stroke_rect_xy_px = (width_height_px - outlined_stroke_width_px) / 2;

    // Draw outline-width layer first
    bl_context_set_fill_style_rgba32(&bl_ctx, m_cursor_outline_color.value);
    bl_context_fill_rect_i(
        &bl_ctx,
        &(BLRectI){
            .x = 0,
            .y = outlined_stroke_rect_xy_px,
            .w = width_height_px,
            .h = outlined_stroke_width_px,
        }
    );
    bl_context_fill_rect_i(
        &bl_ctx,
        &(BLRectI){
            .x = outlined_stroke_rect_xy_px,
            .y = 0,
            .w = outlined_stroke_width_px,
            .h = width_height_px,
        }
    );

    // Then draw smaller main cursor body over it
    bl_context_set_fill_style_rgba32(&bl_ctx, color.value);
    bl_context_fill_rect_i(
        &bl_ctx,
        &(BLRectI){
            .x = outline_width_px,
            .y = stroke_rect_xy_px,
            .w = width_height_px - 2 * outline_width_px,
            .h = stroke_width_px,
        }
    );
    bl_context_fill_rect_i(
        &bl_ctx,
        &(BLRectI){
            .x = stroke_rect_xy_px,
            .y = outline_width_px,
            .w = stroke_width_px,
            .h = width_height_px - 2 * outline_width_px,
        }
    );

    bl_context_end(&bl_ctx);
    bl_context_destroy(&bl_ctx);
}

bool
cursor_reinit(struct scran_output *st_output)
{
    struct scran_cursor *cursor = &st_output->cursor;
    double scale = st_output->selection_surface.surface.final_scale_factor_normalized;

    if (scale == 0) {
        eprintf("Warning: reinit_cursor() got scale=0; using scale=1\n");
        scale = 1;
    }

    // Clamp since we use compile-time buffer sizes
    int width_height_px = MAX(
        1,
        MIN(round(SCRAN_CURSOR_WIDTH_HEIGHT * scale), SCRAN_CURSOR_BUFFER_WIDTH_HEIGHT_PX)
    );
    cursor->width_height_px = width_height_px;

    wp_viewport_set_source(
        cursor->viewport,
        wl_fixed_from_int(0),
        wl_fixed_from_int(0),
        wl_fixed_from_int(width_height_px),
        wl_fixed_from_int(width_height_px)
    );
    wp_viewport_set_destination(
        cursor->viewport,
        SCRAN_CURSOR_WIDTH_HEIGHT,
        SCRAN_CURSOR_WIDTH_HEIGHT
    );

    // Scale events can arrive before shared memory allocation is complete.
    if (cursor->buffers[0].scran_wl_buffer.data) {
        for (int i = 0; i < SCRAN_CURSOR_N_THEMES; ++i) {
            draw_cursor(&cursor->buffers[i], width_height_px, m_cursor_colors[i]);
        }
        cursor_set_theme(st_output, cursor->theme);
    }

    return true;
}

void
cursor_set_theme(
    struct scran_output *st_output,
    enum scran_cursor_theme theme
) {
    struct scran_cursor *cursor    = &st_output->cursor;
    struct wl_buffer    *wl_buffer = cursor->buffers[theme].scran_wl_buffer.wl_buffer;
    assert(wl_buffer != NULL);

    // Store the theme, since pointer::leave/enter events need the cursor to be re-set.
    // Also for cursor_reinit().
    cursor->theme = theme;

    bool have_pointer_focus =
        g_state.seat.pointer_ctx.focused_selection_surface == &st_output->selection_surface;

    if (have_pointer_focus) {
        wl_pointer_set_cursor(
            g_state.seat.wl_pointer,
            g_state.seat.pointer_ctx.last_enter_serial,
            cursor->wl_surface,
            SCRAN_CURSOR_WIDTH_HEIGHT / 2,
            SCRAN_CURSOR_WIDTH_HEIGHT / 2
        );
        wl_surface_attach(cursor->wl_surface, wl_buffer, 0, 0);
        wl_surface_damage_buffer(
            cursor->wl_surface,
            0,
            0,
            cursor->width_height_px,
            cursor->width_height_px
        );
        wl_surface_commit(cursor->wl_surface);
    }
}
