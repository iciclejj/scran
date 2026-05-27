#include <wayland-client-core.h>

#include "ext-image-copy-capture-v1.h"

#include "scranrot.h"

#include "state.h"
#include "state-util.h"
#include "freezeframe.h"
#include "event-handlers.h"
#include "selection-surface.h"
#include "selection.h"
#include "print.h"
#include "util/lib-interop.h"


extern struct scran g_state;


static void
handle_image_copy_capture_frame_transform__freezeframe(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct scran_output *st_output   = data;
    (void)st_output;

    // TODO: What is this transform representing?
    //           It is separate from output::geometry's transform.
    //           TODO: Or is it..?
    assert(transform == st_output->transform);
}

static void handle_image_copy_capture_frame_damage__freezeframe(void *data, struct ext_image_copy_capture_frame_v1 *frame, int32_t x, int32_t y, int32_t width, int32_t height) { }
static void handle_image_copy_capture_frame_presentation_time__image_capture_freezeframe(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) { }

static inline void
_continue_after_showing_freezeframe(
    struct scran_output *st_output,
    enum scran_freezeframe_state state_at_handler_entry
) {

    // Show the selection again
    // NOTE: Freezeframe takes over responsibility for initial selection
    //       draw when freezeframe is enabled!
    //       - See comment in init_postmem__selection() for more details.
    //       Similar with focus retake.
    switch (state_at_handler_entry) {
    case SCRAN_FREEZEFRAME_UNINITIALIZED:
        init_selection_surface_content(st_output);
        break;
    case SCRAN_FREEZEFRAME_REFRESH_REQUESTED_PENDING_REFOCUS:
        refresh_freezeframe__finally(st_output);
        start_grabbing_focus_for_output(st_output);
        break;
    default:
        assert(0 && "Unexpected freezeframe state in freezeframe frame::ready() handler");
    }
}

static void
handle_image_copy_capture_frame_ready__freezeframe(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    ext_image_copy_capture_frame_v1_destroy(frame);
    DEBUG("handle_image_copy_capture_frame_ready__freezeframe()\n");

    struct scran_output             *st_output   = data;
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    enum scran_freezeframe_state prev_state = freezeframe->state;

    struct scran_output_freezeframe_buffer *buffer_to_display;
    enum wl_output_transform buffer_transform = -1;

    // XXX TODO: Rework this once scranrot supports flipped

    // Show the new, just-captured freezeframe
    if (st_output->transform == WL_OUTPUT_TRANSFORM_NORMAL || st_output->transform == WL_OUTPUT_TRANSFORM_FLIPPED) {
        buffer_to_display = &freezeframe->capture_buffer;
        buffer_transform  = st_output->transform;
    } else {
        struct scran_output_freezeframe_buffer *buffer_scranrot_dst = &freezeframe->surface_buffer;

        bool output_is_flipped = st_output->transform >= 4;
        enum wl_output_transform scranrot_transform = output_is_flipped ? st_output->transform - 4 : st_output->transform;

        size_t dst_stride = 0;

        // See comments referencing #14441 for why we scranrot instead of just ::set_buffer_transform().
        if (scranrot_transform_framebuffer(
                freezeframe->capture_buffer.data,
                st_output->mode.width_px, st_output->mode.height_px, st_output->mode.width_px * RGBA32_PIXEL_STRIDE,
                buffer_scranrot_dst->data,
                RGBA32_SHUFFLE_NO_CHANGE, (enum scranrot_transform)scranrot_transform,
                &dst_stride)
        ) {
            assert(dst_stride < INT_MAX && (int)dst_stride == get_transformed_output_width(st_output) * RGBA32_PIXEL_STRIDE);
            buffer_to_display = buffer_scranrot_dst;
            buffer_transform  = output_is_flipped ? WL_OUTPUT_TRANSFORM_FLIPPED : WL_OUTPUT_TRANSFORM_NORMAL;
        } else {
            eprintf("WARNING: Scranrot failed to convert freezeframe buffer; falling back to set_buffer_transform.\n");
            // XXX TODO(1/2): This does not work correctly yet without an actual surface.transform
            // property to check against in the update_scale_size_viewport() functions.
            buffer_to_display = &freezeframe->capture_buffer;
            buffer_transform  = st_output->transform;
        }
    }

    wl_surface_attach(
        freezeframe->surface.wl_surface,
        buffer_to_display->wl_buffer,
        0, 0
    );
    wl_surface_set_buffer_transform(freezeframe->surface.wl_surface, buffer_transform);

    wl_surface_damage_buffer(
        freezeframe->surface.wl_surface,
        0, 0,
        // XXX TODO(2/2):
        get_transformed_width( freezeframe->surface.width_px_buffer, freezeframe->surface.height_px_buffer, buffer_transform),
        get_transformed_height(freezeframe->surface.width_px_buffer, freezeframe->surface.height_px_buffer, buffer_transform)
    );
    wl_surface_commit(freezeframe->surface.wl_surface);
    freezeframe->state = SCRAN_FREEZEFRAME_SHOWING;

    _continue_after_showing_freezeframe(st_output, prev_state);
}

static void
handle_image_copy_capture_frame_failed__freezeframe(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    eprintf("ERROR: freezeframe capture failed (%d)\n", reason);

    struct scran_output             *st_output   = data;
    struct scran_output_freezeframe *freezeframe = &st_output->freezeframe;

    // FIXME: Handle this better

    enum scran_freezeframe_state prev_state = freezeframe->state;
    freezeframe->state = SCRAN_FREEZEFRAME_SHOWING;

    _continue_after_showing_freezeframe(st_output, prev_state);
}

struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__freezeframe = {
    .transform = handle_image_copy_capture_frame_transform__freezeframe,
    .damage = handle_image_copy_capture_frame_damage__freezeframe,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__image_capture_freezeframe,
    .ready = handle_image_copy_capture_frame_ready__freezeframe,
    .failed = handle_image_copy_capture_frame_failed__freezeframe,
};
