#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/stat.h>

#include <ext-image-copy-capture-v1.h>

#include "dbus.h"
#include "state.h"
#include "state-util.h" // TODO: Move this into util/ ?
#include "util/util.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "capture.h"
#include "print.h"
#include "scranrot.h"
#include "event-handlers.h"
#include "options.h"
#include "clipboard.h"


static void
handle_image_copy_capture_frame_transform__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct scran_output *st_output = data;
    st_output->capture.frame_ctx.source_transform = transform;
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

// TODO: Make the generic end_capture logic shared between image and video
static inline void
end_capture(struct scran_output *st_output)
{
    if (st_output->capture.frame_ctx.fullscreen_capture) {
        end_fullscreen_capture(st_output);
    }

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


// TODO:
// - Maybe see the top TODO in the __video_capture handler
// - See if we can use frame_ctx or a new frame_ctx separate from video
//   capture, rather than the entire st_capture struct.
//       Not as important for just image capture as with video capture, though
// - Error handling or robust asserts
// - Let user decide encoding parameters etc.
// - Arg/option to choose: save to file only, clipboard only, or both.
static void
handle_image_copy_capture_frame_ready__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct scran_output          *st_output = data;
    struct capture_frame_context *frame_ctx = &st_output->capture.frame_ctx;
    struct capture_session       *session   = &st_output->capture.session;

    const BLBoxI capture_buffer_area_px = capture_get_selection_as_capture_buffer_area_px(frame_ctx, session);
    const int    capture_buffer_area_px_w  = blboxi_width_abs_unsafe(capture_buffer_area_px);
    const int    capture_buffer_area_px_h  = blboxi_height_abs_unsafe(capture_buffer_area_px);

    DEBUG("CAPTURING IMAGE:\n");
    DEBUG_BLBOXI(capture_buffer_area_px);
    // XXX: Capturing image during video capture not implemented yet...
    assert(!frame_ctx->capturing_video);
    assert(g_state.n_captures_in_progress >= 1);

    const uint32_t source_row_bytes = session->pixel_stride * session->source_dimensions_px.x;
    // XXX TODO: Either separate buffer from video capture OR double-check that
    // the shared buffer doesn't cause issues + add robust checks/asserts.
    // (Primarily for when we implement simultaneous image+video capture)
    const uint8_t *const area_start_addr = capture_get_area_start_address(frame_ctx, session, capture_buffer_area_px);
    // XXX TODO(!!):
    //    Output size is not necessarily guaranteed to be <= raw pixel
    //    buffer size. In other words, this buffer could overflow, as it
    //    is (at time of writing) set to equal the size of the raw
    //    capture source pixel buffer.
    void *const buf_cropped_converted = frame_ctx->img_data_2;

    uintptr_t buf_cropped_converted_row_bytes = 0;
    uint32_t  rgba32_shuffle = wl_shm_format_to_blend2d_scranrot_rgba32_shuffle(st_output->capture.session.shm_format);
    if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
        eprintf("WARNING: Output's pixel format is not supported. Attempting anyways...\n");
        rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
    }

    // XXX: Scranrot does not support flipped transforms yet, so we just
    // record it flipped for now, rather than blocking capture entirely.
    enum wl_output_transform transform = wl_output_transform_without_flip(frame_ctx->source_transform);

    // XXX: We convert etc. unconditionally for now.
    //    TODO: Only convert if required
    //            I.e. convert if not natively supported pixel format by blend2d
    //            encoder and/or needs transform
    if (!scranrot_transform_framebuffer(
            area_start_addr, capture_buffer_area_px_w, capture_buffer_area_px_h, source_row_bytes,
            buf_cropped_converted,
            rgba32_shuffle,
            wl_output_transform_to_scranrot(transform),
            &buf_cropped_converted_row_bytes
        )
    ) {
        eprintf("Error: scranrot failed to convert framebuffer\n");
        goto end_capture;
    }
    const int final_image_width  = get_transformed_width( capture_buffer_area_px_w, capture_buffer_area_px_h, transform);
    const int final_image_height = get_transformed_height(capture_buffer_area_px_w, capture_buffer_area_px_h, transform);


    // Encode
    BLResult res;

    // NOTE: The data passed is not freed unless freed by passed destroy_func,
    // if it is not NULL (aka it is not freed here, at time of writing).
    res = bl_image_create_from_data(
        &frame_ctx->bl_img_captured,
        final_image_width,
        final_image_height,
        IMAGE_CAPTURE_OUTPUT_BLFORMAT_DEFAULT,
        buf_cropped_converted,
        buf_cropped_converted_row_bytes,
        // XXX: Read-only access causes blend2d to make a copy if modified.
        //      TODO: Probably just change to RW.
        BL_DATA_ACCESS_READ,
        NULL,
        NULL
    );
    DEBUG("image_copy_capture_frame.c: bl_image_create_from_data:  %d\n", res);

    // TODO: This should only be called once, outside of the capture event
    // pipeline, unless between-capture format changing is implemented.
    res = bl_image_codec_find_by_name(&frame_ctx->bl_imgcodec, IMAGE_CAPTURE_OUTPUT_BLIMAGECODEC_NAME_DEFAULT, SIZE_MAX, NULL);
    // TODO: This should be initialized in init_premem, so we don't re-allocate
    // the array backing every time. Must in that case either be a double-
    // buffer, OR assert that there will never be a race condition with
    // offer::send.
    //     Probably simply doing everything within one run of this function
    //     should to be enough, assuming no multi-threading?
    BLArrayCore bl_array_img_encoded;
    bl_array_init(&bl_array_img_encoded, BL_OBJECT_TYPE_ARRAY_UINT8);
    res = bl_image_write_to_data(&frame_ctx->bl_img_captured, &bl_array_img_encoded, &frame_ctx->bl_imgcodec);

    // TODO: Are the internal functions reasonably stable and/or easy to use
    //       directly?
    //           blend2d is not very pretty in plain C for what we're doing
    //           here and in the rest of this function...
    const void *const bl_array_img_encoded_data = bl_array_get_data(&bl_array_img_encoded);
    const size_t bytes_to_write = bl_array_get_size(&bl_array_img_encoded);

    struct scran_options *const st_options = &g_state.options;
    const char *output_filepath = NULL;

    if (st_options->output_to_stdout) {
        // TODO: Assert nothing else was written to stdout?
        if (!scran_full_write(STDOUT_FILENO, bl_array_img_encoded_data, bytes_to_write)) {
            eprintf("Failed to write image to stdout.\n");
        }
    } else {
        static const char default_extension[SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX] =
            IMAGE_CAPTURE_OUTPUT_FILE_EXTENSION_DEFAULT;
        output_filepath =
            scran_update_output_filepath(st_options, default_extension);

        size_t bytes_written = 0;
        res = bl_file_system_write_file(
            output_filepath,
            bl_array_img_encoded_data,
            bytes_to_write,
            &bytes_written
        );

        if (res == BL_SUCCESS && bytes_written == bytes_to_write) {
            eprintf("Image saved: %s (%zuKiB)\n", output_filepath, bytes_written >> 10);
            scran_portal_notify_file_saved(output_filepath);
        } else {
            eprintf("Error: Failed to save image (attempted: %s).\n", output_filepath);
        }
    }

    // TODO: Is this the inteded way for a user to access members not exposed to
    // the C-API by bl_*_get_* functions ?
    //     See: https://blend2d.com/doc/group__bl__impl.html
    const BLImageCodecImpl *const bl_img_codec_impl = (BLImageCodecImpl *)(frame_ctx->bl_imgcodec._d.impl);
    const char *mime_type = bl_string_get_data(&bl_img_codec_impl->mime_type);

    if (!clipboard_update(&g_state.seat.datacontrol, &bl_array_img_encoded, mime_type, output_filepath)) {
        eprintf("Error updating clipboard.\n");
    }

    bl_array_destroy(&bl_array_img_encoded);

end_capture:
    end_capture(st_output);
}


static void
handle_image_copy_capture_frame_failed__image_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct scran_output *st_output = data;

    end_capture(st_output);
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__image_capture = {
    .transform = handle_image_copy_capture_frame_transform__image_capture,
    .damage = handle_image_copy_capture_frame_damage__image_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__image_capture,
    .ready = handle_image_copy_capture_frame_ready__image_capture,
    .failed = handle_image_copy_capture_frame_failed__image_capture,
};
