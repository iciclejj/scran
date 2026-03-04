#include <assert.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include <ext-image-copy-capture-v1.h>

#include "ext-data-control-v1.h"
#include "state.h"
#include "state-util.h" // TODO: Move this into util/ ?
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "capture.h"
#include "print.h"
#include "simd.h"
#include "event-handlers.h"
#include "options.h"

extern struct scran g_state;

static void
handle_image_copy_capture_frame_transform__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;

    // TODO: What is this transform representing?
    //           It is separate from output::geometry's transform.
}


static void
handle_image_copy_capture_frame_damage__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    // No-op
    //
    // TODO: Maybe make it not a no-op, depending on how simultaneous
    // image + video capture gets implemented.
}


static void
handle_image_copy_capture_frame_presentation_time__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    // No-op
}


// TODO:
// - Maybe see the top TODO in the __video_capture handler
// - See if we can use frame_ctx or a new frame_ctx separate from video
//   capture, rather than the entire st_capture struct.
//       Not as important for just image capture as with video capture, though
// - Error handling or robust asserts
// - Let user decide encoding parameters etc.
static void
handle_image_copy_capture_frame_ready__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    struct scran_output_capture *st_capture = data;
    struct capture_frame_context *frame_ctx = &st_capture->frame_ctx;

    // XXX: Capturing image during video capture implemented yet...
    assert(!frame_ctx->capturing_video);
    assert(g_state.n_captures_in_progress >= 1);

    ext_image_copy_capture_frame_v1_destroy(frame);


    // TODO: Do we ever actually need to call blend2d *_reset() fuctions before
    // re-entry into this event handler?

    BLResult res;

    // TODO: Clarify names, more in sync with start_capture names?
    const int area_width_no_transform = blboxi_width_abs_unsafe(frame_ctx->capture_area_px);
    const int area_height_no_transform = blboxi_height_abs_unsafe(frame_ctx->capture_area_px);
    const uint32_t area_row_bytes = frame_ctx->pixel_stride * area_width_no_transform;
    const uint32_t source_row_bytes = frame_ctx->pixel_stride * frame_ctx->source_width_px;
    // XXX TODO: Either separate buffer from video capture OR double-check that
    // the shared buffer doesn't cause issues + add robust checks/asserts.
    // (Primarily for when we implement simultaneous image+video capture)
    const uint8_t *const area_start_addr =
        frame_ctx->st_buffer.data
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;

    // TODO: Double-check that this pointer doesn't get overwritten by blend2d
    //       and re-allocated.
    //       ALSO XXX TODO(!!):
    //           Output size is not necessarily guaranteed to be <= raw pixel
    //           buffer size. In other words, this buffer could overflow, as it
    //           is (at time of writing) set to equal the size of the raw
    //           capture source pixel buffer.
    void *const bl_buf_cropped_converted = frame_ctx->img_data_2;

    // XXX TODO: Rework the frame_ctx struct. Probably also just pass entire
    // scran_output as the listener callback data.
    struct scran_output *st_output = (void*)((char*)frame_ctx - (offsetof(struct scran_output, capture) + offsetof(struct scran_output_capture, frame_ctx)));

    // XXX: We just always run it through the converter for now.
    // TODO: Only convert if required (not natively supported pixel format by blend2d)
    // TODO: Probably make some way to easily get padded height/widths etc.
    //       with some centralized source of truth
    // TODO: Assert we have available padding.
    void *bl_buf_cropped_converted_with_offset = NULL;
    uintptr_t bl_buf_cropped_converted_row_bytes = 0;
    uint32_t rgba32_shuffle = wl_shm_format_to_blend2d_scran_rgba32_shuffle(st_output->capture.shm_format);

    if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
        eprintf("WARNING: Output's pixel format is not supported. Attempting anyways...");
        rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
    }

    // TODO: More asserts before & after this + double-checking the padding and
    // alignment logic both within transform_framebuffer after returning
    transform_framebuffer(
        area_start_addr,
        bl_buf_cropped_converted,
        area_width_no_transform,
        area_height_no_transform,
        source_row_bytes,
        rgba32_shuffle,
        st_output->transform,
        &bl_buf_cropped_converted_with_offset,
        &bl_buf_cropped_converted_row_bytes
    );
    const int area_width_transformed = get_transformed_width(area_width_no_transform, area_height_no_transform, st_output->transform);
    const int area_height_transformed = get_transformed_height(area_width_no_transform, area_height_no_transform, st_output->transform);


    // NOTE: The data passed is not freed unless freed by passed destroy_func,
    // if it is not NULL (aka it is not freed here, at time of writing).
    // const int _post_inverse_transform_area_width = area_height_no_transform;
    // const int _post_inverse_transform_area_height = area_width_no_transform;
    res = bl_image_create_from_data(
        &frame_ctx->bl_img_captured,
        area_width_transformed,
        area_height_transformed,
        CAPTURE_IMAGE_OUTPUT_BLFORMAT_DEFAULT,
        bl_buf_cropped_converted_with_offset,
        bl_buf_cropped_converted_row_bytes,
        // XXX: Read-only access causes blend2d to make a copy if modified.
        // TODO: Probably just change to RW.
        BL_DATA_ACCESS_READ,
        NULL,
        NULL
    );
    DEBUG("image_copy_capture_frame.c: bl_image_init_as_from_data:  %d\n", res);

    // TODO: This should only be called once, outside of the capture event
    // pipeline, unless between-capture format changing is implemented.
    res = bl_image_codec_find_by_name(&frame_ctx->bl_imgcodec, CAPTURE_IMAGE_OUTPUT_BLIMAGECODEC_NAME_DEFAULT, SIZE_MAX, NULL);

    // TODO: This should be initialized in init_premem, so we don't re-allocate
    // the array backing every time. Must in that case either be a double-
    // buffer, OR assert that there will never be a race condition with
    // offer::send.
    //     Probably simply doing everything within one run of this function
    //     should to be enough, assuming no multi-threading?
    BLArrayCore bl_array_img_encoded;
    bl_array_init(&bl_array_img_encoded, BL_OBJECT_TYPE_ARRAY_UINT8);
    res = bl_image_write_to_data(&frame_ctx->bl_img_captured, &bl_array_img_encoded, &frame_ctx->bl_imgcodec);

    // TODO: Arg/option to choose: save to file only, clipboard only, or both.

    // TODO: Are the internal functions reasonably stable and/or easy to use
    //       directly?
    //           blend2d is not very pretty in plain C for what we're doing
    //           here and in the rest of this function...
    const void *const bl_array_img_encoded_data = bl_array_get_data(&bl_array_img_encoded);
    const size_t bytes_to_write = bl_array_get_size(&bl_array_img_encoded);
    size_t bytes_written;

    const struct scran_options *const st_options = &g_state.options;

    scran_update_output_filepath(st_options, CAPTURE_IMAGE_OUTPUT_FILE_EXTENSION_DEFAULT);
    res = bl_file_system_write_file(
        st_options->output_filepath,
        bl_array_img_encoded_data,
        bytes_to_write,
        &bytes_written
    );

    if (res == BL_SUCCESS && bytes_written == bytes_to_write) {
        eprintf("Image saved: %s (%ldKiB)\n", st_options->output_filepath, bytes_written >> 10);
    } else {
        eprintf("Error: Failed to save image (attempted: %s).", st_options->output_filepath);
    }


    // TODO: Consider loading image back from storage, instead of storing it
    // in memory? At least for videos, which we probably won't keep in memory.

    // TODO: Verify that init_move doesn't leak memory without explicit reset
    //           And also that the moved-*from* instance doesn't need explicit
    //           destroy
    bl_array_init_move(&frame_ctx->st_datacontrol->data_to_send, &bl_array_img_encoded);
    // TODO: Is this the inteded way for a user to access members not exposed to
    // the C-API by bl_*_get_* functions ?
    //     See: https://blend2d.com/doc/group__bl__impl.html
    const BLImageCodecImpl *const bl_img_codec_impl = (BLImageCodecImpl *)(frame_ctx->bl_imgcodec._d.impl);
    // TODO: Double-check (lack of) refcounting behavior of _get_data functions
    //           XXX TODO: Also, if not refcounted, then ensure it is nulled
    //           when invalidated or that it will not matter that it isn't.
    frame_ctx->st_datacontrol->data_to_send_mime_type = bl_string_get_data(&bl_img_codec_impl->mime_type);

    struct ext_data_control_source_v1 *data_control_source =
        ext_data_control_manager_v1_create_data_source(
            *frame_ctx->st_datacontrol->manager
        );
    ext_data_control_source_v1_add_listener(
        data_control_source,
        &data_control_source_listener,
        frame_ctx->st_datacontrol
    );
    ext_data_control_source_v1_offer(
        data_control_source,
        frame_ctx->st_datacontrol->data_to_send_mime_type
    );
    DEBUG("image_capture::frame: set_selection (clipboard)\n");
    ext_data_control_device_v1_set_selection(frame_ctx->st_datacontrol->device, data_control_source);
    // TODO: Relaxed might not end up being enough. Revisit this if we ever do
    // go multithreaded (atomics are not doing anything useful at the moment).
    atomic_fetch_add_explicit(&frame_ctx->st_datacontrol->selection_refcount, 1, memory_order_relaxed);

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__image_capture = {
    .transform = handle_image_copy_capture_frame_transform__image_capture,
    .damage = handle_image_copy_capture_frame_damage__image_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__image_capture,
    .ready = handle_image_copy_capture_frame_ready__image_capture,
};

