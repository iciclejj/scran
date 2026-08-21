#include <wayland-client-core.h>

#include "ext-image-copy-capture-v1.h"

#include "scranrot.h"

#include "state.h"
#include "state-util.h"
#include "selection.h"
#include "event-handlers.h"
#include "print.h"
#include "ui.h"
#include "util/lib-interop.h"


extern struct scran g_state;


static void
handle_image_copy_capture_frame_transform__freezeframe(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct scran_output *st_output = data;
    st_output->freezeframe.source_transform = transform;
}

static void handle_image_copy_capture_frame_damage__freezeframe(void *data, struct ext_image_copy_capture_frame_v1 *frame, int32_t x, int32_t y, int32_t width, int32_t height) { }
static void handle_image_copy_capture_frame_presentation_time__image_capture_freezeframe(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) { }

static inline void
continue_after_showing_freezeframe(
    struct scran_output *st_output
) {
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    if (freezeframe->unhide_after_capture) {
        release_selection_surface_hide(st_output, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FREEZEFRAME_HIDE);
        freezeframe->unhide_after_capture = false;
    }

    freezeframe_callback callback = freezeframe->callback;
    assert(callback != NULL);
    freezeframe->callback = NULL;
    callback(st_output);
}

static void
display_freezeframe(
    struct scran_output *st_output
) {
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    struct scran_freezeframe_buffer *capture_buffer = &freezeframe->capture_buffer;
    struct scran_freezeframe_buffer *surface_buffer = &freezeframe->surface_buffer;

    assert(capture_buffer->busy == false); // We should not have started capture if busy

    struct scran_freezeframe_buffer *final_buffer;
    enum wl_output_transform buffer_transform = -1;

    const enum wl_output_transform source_transform = freezeframe->source_transform;
    const int32_t                  source_width_px  = freezeframe->source_width_px;
    const int32_t                  source_height_px = freezeframe->source_height_px;

    // XXX TODO: Rework this once scranrot supports flipped
    // XXX TODO: Refactor this to make it more readable...

    // Show the new, just-captured freezeframe
    if (source_transform == WL_OUTPUT_TRANSFORM_NORMAL || source_transform == WL_OUTPUT_TRANSFORM_FLIPPED) {
        final_buffer     = capture_buffer;
        buffer_transform = source_transform;
    } else {
        bool source_is_flipped = source_transform >= WL_OUTPUT_TRANSFORM_FLIPPED;
        enum wl_output_transform scranrot_transform = source_is_flipped ? source_transform - WL_OUTPUT_TRANSFORM_FLIPPED : source_transform;

        if (surface_buffer->busy) {
            surface_buffer->release_callback = display_freezeframe;
            return;
        }

        assert(get_transformed_width(source_width_px, source_height_px, source_transform) == freezeframe->subsurface.width_px_buffer);
        assert(get_transformed_height(source_width_px, source_height_px, source_transform) == freezeframe->subsurface.height_px_buffer);

        size_t dst_stride = 0;
        // See comments referencing #14441 for why we scranrot instead of just ::set_buffer_transform().
        if (scranrot_transform_framebuffer(
                capture_buffer->scran_wl_buffer.data, source_width_px, source_height_px, source_width_px * RGBA32_PIXEL_STRIDE,
                surface_buffer->scran_wl_buffer.data,
                RGBA32_SHUFFLE_NO_CHANGE, (enum scranrot_transform)scranrot_transform,
                &dst_stride)
        ) {
            assert(dst_stride < INT_MAX && (int)dst_stride == freezeframe->subsurface.width_px_buffer * RGBA32_PIXEL_STRIDE);
            final_buffer     = surface_buffer;
            buffer_transform = source_is_flipped ? WL_OUTPUT_TRANSFORM_FLIPPED : WL_OUTPUT_TRANSFORM_NORMAL;
        } else {
            eprintf("WARNING: Scranrot failed to convert freezeframe buffer; falling back to set_buffer_transform.\n");
            // XXX TODO: This does not work correctly yet without an actual surface.transform
            // property to check against in the update_scale_size_viewport() functions.
            final_buffer     = capture_buffer;
            buffer_transform = source_transform;
        }
    }
    const int final_width_px  = (final_buffer == capture_buffer) ? source_width_px  : freezeframe->subsurface.width_px_buffer;
    const int final_height_px = (final_buffer == capture_buffer) ? source_height_px : freezeframe->subsurface.height_px_buffer;

    final_buffer->busy = true;
    wl_surface_attach(freezeframe->subsurface.wl_surface, final_buffer->scran_wl_buffer.wl_buffer, 0, 0);
    wl_surface_set_buffer_transform(freezeframe->subsurface.wl_surface, buffer_transform);
    wl_surface_damage_buffer(freezeframe->subsurface.wl_surface, 0, 0, final_width_px, final_height_px);
    wl_surface_commit(freezeframe->subsurface.wl_surface);
    freezeframe->showing = true;
    {
        struct scran_ui_context *ui_ctx = &st_output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_text( ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_OFF);
        scran_ui_textline_item_set_color(ui_ctx, SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME);
    }

    continue_after_showing_freezeframe(st_output);
}

static void
handle_image_copy_capture_frame_ready__freezeframe(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    ext_image_copy_capture_frame_v1_destroy(frame);
    struct scran_output *st_output = data;
    display_freezeframe(st_output);
}

static void
handle_image_copy_capture_frame_failed__freezeframe(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    eprintf("ERROR: freezeframe capture failed (%d)\n", reason);

    struct scran_output *st_output = data;

    // FIXME: Handle this better?
    continue_after_showing_freezeframe(st_output);
}

struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__freezeframe = {
    .transform = handle_image_copy_capture_frame_transform__freezeframe,
    .damage = handle_image_copy_capture_frame_damage__freezeframe,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__image_capture_freezeframe,
    .ready = handle_image_copy_capture_frame_ready__freezeframe,
    .failed = handle_image_copy_capture_frame_failed__freezeframe,
};
