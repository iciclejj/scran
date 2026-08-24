#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>

#include <ext-image-copy-capture-v1.h>

#include "capture.h"
#include "clipboard.h"
#include "dbus.h"
#include "event-handlers.h"
#include "options.h"
#include "print.h"
#include "selection.h"
#include "scranrot.h"
#include "state.h"
#include "state-util.h"
#include "ui.h"
#include "util/blend2d.h"
#include "util/lib-interop.h"
#include "util/util.h"


struct capture_buffer_area_context {
    const uint8_t *area_start_address;
    BLBoxI area_px;
    uint32_t source_row_bytes;
};


static inline void
capture_create_buffer_area_context(
    const struct scran_output *output,
    const struct capture_session_context *session,
    const struct capture_frame_context *frame_ctx,
    struct capture_buffer_area_context *buffer_area_ctx
) {
    buffer_area_ctx->area_px = capture_get_selection_as_capture_buffer_area_px(
        &output->capture,
        session,
        frame_ctx
    );
    buffer_area_ctx->area_start_address = capture_get_area_start_address(
        session,
        frame_ctx,
        &buffer_area_ctx->area_px
    );
    buffer_area_ctx->source_row_bytes =
        session->pixel_stride * session->source_dimensions_px.x;
}


static void
handle_image_copy_capture_frame_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;
    frame_ctx->source_transform = transform;
}


static void
handle_image_copy_capture_frame_damage(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    struct capture_frame_context *frame_ctx = data;
    capture_grow_tracked_damage(frame_ctx, x, y, width, height);
}


static void
handle_image_copy_capture_frame_presentation_time(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    struct capture_frame_context *frame_ctx = data;

    // XXX: Will overflow at tv_sec > ~584.9 years...
    const int64_t tv_sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = tv_sec_to_nsec + tv_nsec;
}


static inline void
end_image_capture(struct scran_output *output)
{
    if (output->capture.fullscreen_capture) {
        end_fullscreen_capture(output);
    }

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}


static inline void
continue_after_showing_freezeframe(
    struct scran_output *output
) {
    struct scran_output_freezeframe *freezeframe = &output->freezeframe;

    if (freezeframe->unhide_after_capture) {
        release_selection_surface_hide(output, SCRAN_SELECTION_SURFACE_DISABLE_REASON_FREEZEFRAME_HIDE);
        freezeframe->unhide_after_capture = false;
    }

    freezeframe_callback callback = freezeframe->callback;
    assert(callback != NULL);
    freezeframe->callback = NULL;
    callback(output);
}


static void
display_freezeframe(
    struct scran_output *output
) {
    struct scran_output_freezeframe *freezeframe = &output->freezeframe;

    struct scran_freezeframe_buffer *capture_buffer = &freezeframe->capture_buffer;
    struct scran_freezeframe_buffer *surface_buffer = &freezeframe->surface_buffer;
    const struct capture_session_context *session = &freezeframe->session.session_ctx;

    assert(capture_buffer->busy == false); // We should not have started capture if busy

    struct scran_freezeframe_buffer *final_buffer;

    enum wl_output_transform       buffer_transform = -1;
    const int32_t                  source_width_px  = session->source_dimensions_px.x;
    const int32_t                  source_height_px = session->source_dimensions_px.y;
    const enum wl_output_transform source_transform = freezeframe->session.frame_ctx.source_transform;

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
                capture_buffer->scran_wl_buffer.data, source_width_px, source_height_px, source_width_px * session->pixel_stride,
                surface_buffer->scran_wl_buffer.data,
                RGBA32_SHUFFLE_NO_CHANGE, (enum scranrot_transform)scranrot_transform,
                &dst_stride)
        ) {
            assert(dst_stride < INT_MAX && (int)dst_stride == freezeframe->subsurface.width_px_buffer * session->pixel_stride);
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
        struct scran_ui_context *ui_ctx = &output->selection_surface.ui_ctx;
        scran_ui_textline_item_set_text( SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_TEXT_KEYMAP_FREEZEFRAME_TURN_OFF);
        scran_ui_textline_item_set_color(SCRAN_UI_TEXTLINE_VIEW(ui_ctx->ui_keymap), SCRAN_UI_KEYMAP_ITEM_I_FREEZEFRAME, SCRAN_UI_COLOR_KEYMAP_FREEZEFRAME);
    }

    continue_after_showing_freezeframe(output);
}


static void
do_handle_image_frame(
    struct scran_output *output,
    const struct capture_session_context *session,
    const struct capture_frame_context *frame_ctx,
    const struct capture_buffer_area_context *buffer_area_ctx
) {
    const int capture_buffer_area_px_w = blboxi_width_abs_unsafe(buffer_area_ctx->area_px);
    const int capture_buffer_area_px_h = blboxi_height_abs_unsafe(buffer_area_ctx->area_px);

    DEBUG("CAPTURING IMAGE:\n");
    DEBUG_BLBOXI(buffer_area_ctx->area_px);

    // XXX TODO(!!):
    //    Output size is not necessarily guaranteed to be <= raw pixel
    //    buffer size. In other words, this buffer could overflow, as it
    //    is (at time of writing) set to equal the size of the raw
    //    capture source pixel buffer.
    void *const buf_cropped_converted = output->capture.img_data_2;

    uintptr_t buf_cropped_converted_row_bytes = 0;
    uint32_t rgba32_shuffle =
        wl_shm_format_to_blend2d_scranrot_rgba32_shuffle(session->shm_format);
    if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
        eprintf("WARNING: Output's pixel format is not supported. Attempting anyways...\n");
        rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
    }

    // XXX: Scranrot does not support flipped transforms yet, so we just
    // record it flipped for now, rather than blocking capture entirely.
    enum wl_output_transform transform =
        wl_output_transform_without_flip(frame_ctx->source_transform);

    // XXX: We convert etc. unconditionally for now.
    //    TODO: Only convert if required
    //            I.e. convert if not natively supported pixel format by blend2d
    //            encoder and/or needs transform
    if (!scranrot_transform_framebuffer(
            buffer_area_ctx->area_start_address,
            capture_buffer_area_px_w,
            capture_buffer_area_px_h,
            buffer_area_ctx->source_row_bytes,
            buf_cropped_converted,
            rgba32_shuffle,
            wl_output_transform_to_scranrot(transform),
            &buf_cropped_converted_row_bytes
        )
    ) {
        eprintf("Error: scranrot failed to convert framebuffer\n");
        return;
    }
    const int final_image_width = get_transformed_width(
        capture_buffer_area_px_w,
        capture_buffer_area_px_h,
        transform
    );
    const int final_image_height = get_transformed_height(
        capture_buffer_area_px_w,
        capture_buffer_area_px_h,
        transform
    );

    // Encode
    BLResult res;

    // NOTE: The data passed is not freed unless freed by passed destroy_func,
    // if it is not NULL (aka it is not freed here, at time of writing).
    res = bl_image_create_from_data(
        &output->capture.bl_img_captured,
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
    DEBUG("image-copy-capture-frame.c: bl_image_create_from_data: %d\n", res);

    // TODO: This should only be called once, outside of the capture event
    // pipeline, unless between-capture format changing is implemented.
    res = bl_image_codec_find_by_name(
        &output->capture.bl_imgcodec,
        IMAGE_CAPTURE_OUTPUT_BLIMAGECODEC_NAME_DEFAULT,
        SIZE_MAX,
        NULL
    );
    // TODO: This should be initialized in init_premem, so we don't re-allocate
    // the array backing every time. Must in that case either be a double-
    // buffer, OR assert that there will never be a race condition with
    // offer::send.
    //     Probably simply doing everything within one run of this function
    //     should to be enough, assuming no multi-threading?
    BLArrayCore bl_array_img_encoded;
    bl_array_init(&bl_array_img_encoded, BL_OBJECT_TYPE_ARRAY_UINT8);
    res = bl_image_write_to_data(
        &output->capture.bl_img_captured,
        &bl_array_img_encoded,
        &output->capture.bl_imgcodec
    );

    // TODO: Are the internal functions reasonably stable and/or easy to use
    //       directly?
    //           blend2d is not very pretty in plain C for what we're doing
    //           here and in the rest of this function...
    const void *const bl_array_img_encoded_data = bl_array_get_data(&bl_array_img_encoded);
    const size_t bytes_to_write = bl_array_get_size(&bl_array_img_encoded);

    struct scran_options *const options = &g_state.options;
    const char *output_filepath = NULL;

    if (options->output_to_stdout) {
        // TODO: Assert nothing else was written to stdout?
        if (!scran_full_write(STDOUT_FILENO, bl_array_img_encoded_data, bytes_to_write)) {
            eprintf("Failed to write image to stdout.\n");
        }
    } else {
        static const char default_extension[SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX] =
            IMAGE_CAPTURE_OUTPUT_FILE_EXTENSION_DEFAULT;
        output_filepath = scran_update_output_filepath(options, default_extension);

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

    // TODO: Is this the intended way for a user to access members not exposed to
    // the C-API by bl_*_get_* functions?
    //     See: https://blend2d.com/doc/group__bl__impl.html
    const BLImageCodecImpl *const bl_img_codec_impl =
        (BLImageCodecImpl *)(output->capture.bl_imgcodec._d.impl);
    const char *mime_type = bl_string_get_data(&bl_img_codec_impl->mime_type);

    if (!clipboard_update(
            &g_state.seat.datacontrol,
            &bl_array_img_encoded,
            mime_type,
            output_filepath
        )
    ) {
        eprintf("Error updating clipboard.\n");
    }

    bl_array_destroy(&bl_array_img_encoded);
}


static inline bool
do_handle_video_frame(
    struct scran_output *output,
    struct capture_frame_context *frame_ctx,
    const struct capture_session_context *session,
    const struct capture_buffer_area_context *buffer_area_ctx
) {
    if (!blboxi_intersects(buffer_area_ctx->area_px, frame_ctx->capture_buffer_damage_area_px)) {
        return true;
    }

    struct ffmpeg_context *ffmpeg = &output->capture.ffmpeg_ctx;

    // Crop and convert
    {
        // XXX NOTE: Zeroing out the last bit because x264 needs the dimensions to be divisible by 2.
        // XXX TODO: Collect this bit zeroing logic somehow? (Duplicated in init_ffmpeg.)
        const int area_w_px = blboxi_width_abs_unsafe(buffer_area_ctx->area_px) & ~0b1;
        const int area_h_px = blboxi_height_abs_unsafe(buffer_area_ctx->area_px) & ~0b1;

        uint32_t rgba32_shuffle = wl_shm_format_to_scranrot_yuv_rgba32_shuffle(session->shm_format);
        if (rgba32_shuffle == RGBA32_SHUFFLE_ERROR) {
            eprintf(
                "WARNING: Output's pixel format (%x) not recognized. Please report this as a bug. Attempting anyways...\n",
                session->shm_format
            );
            rgba32_shuffle = RGBA32_SHUFFLE_NO_CHANGE;
        }

        // XXX: Scranrot does not support flipped transforms yet, so we just
        // record it flipped for now, rather than blocking capture entirely.
        enum wl_output_transform transform = wl_output_transform_without_flip(frame_ctx->source_transform);

        AVFrame *frame = ffmpeg->av_frame_to_encode;
        void *const frame_buffer = output->capture.img_data_2;

        assert(frame->width == get_transformed_width(area_w_px, area_h_px, transform));
        assert(frame->height == get_transformed_height(area_w_px, area_h_px, transform));

        if (!scranrot_transform_framebuffer_to_yuv420(
                buffer_area_ctx->area_start_address,
                area_w_px,
                area_h_px,
                buffer_area_ctx->source_row_bytes,
                frame_buffer,
                rgba32_shuffle,
                wl_output_transform_to_scranrot(transform),
                &frame->data[0], &frame->linesize[0],
                &frame->data[1], &frame->linesize[1],
                &frame->data[2], &frame->linesize[2]
            )
        ) {
            eprintf("Error: scranrot failed to convert framebuffer to yuv\n");
            return false;
        }
        frame->pts = frame_ctx->presentation_time_nsec - output->capture.video_presentation_time_nsec_start;
    }

    // Encode
    int _ret_enc = avcodec_send_frame(ffmpeg->av_codec_ctx, ffmpeg->av_frame_to_encode);
    assert(_ret_enc != AVERROR(EINVAL));
    while (_ret_enc >= 0) {
        _ret_enc = avcodec_receive_packet(ffmpeg->av_codec_ctx, ffmpeg->av_packet);
        assert(_ret_enc != AVERROR(EINVAL));

        if (_ret_enc == AVERROR_EOF || _ret_enc == AVERROR(EAGAIN)) {
            break;
        } else if (_ret_enc < 0) {
            eprintf("Error while encoding frame\n");
            return false;
        }

        video_capture_write_video_packet(output, ffmpeg->av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    av_packet_unref(ffmpeg->av_packet);
    return true;
}

static inline void
end_video_capture(
    struct scran_output *output
) {
    video_capture_finish(output);

    output->capture.capturing_video = false;
    output->capture.video_end_requested = false;

    DEBUG("FINISHED RECORDING.\n");
}



static void
handle_image_copy_capture_frame_ready(
    void *data,
    struct ext_image_copy_capture_frame_v1 *wl_frame
) {
    ext_image_copy_capture_frame_v1_destroy(wl_frame);

    struct capture_frame_context *frame_ctx = data;
    struct scran_output          *output    = frame_ctx->output;
    frame_ctx->frame = NULL;

    const bool image_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE;
    const bool video_requested       = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO;
    const bool freezeframe_requested = frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME;

    assert(image_requested || video_requested || freezeframe_requested);

    if (freezeframe_requested) {
        display_freezeframe(output);
    }

    if (image_requested || video_requested) {
        const struct capture_session_context *session = &output->capture.session.session_ctx;
        struct capture_buffer_area_context buffer_area_ctx;
        capture_create_buffer_area_context(output, session, frame_ctx, &buffer_area_ctx);

        if (image_requested) {
            // XXX: Capturing image during video capture not implemented yet...
            assert(!output->capture.capturing_video);
            assert(g_state.n_captures_in_progress >= 1);

            do_handle_image_frame(output, session, frame_ctx, &buffer_area_ctx);
            end_image_capture(output);
        }

        if (video_requested) {
            if (!do_handle_video_frame(output, frame_ctx, session, &buffer_area_ctx)) {
                output->capture.video_end_requested = true;
            }

            // NOTE: We do this check *after* writing the incoming frame. This ensures
            // that the video will not be cut short at the end if we're only capturing
            // frames on demand (with variable framerate) and nothing has changed for
            // the last x amount of time.
            // Forcing some compositor/surface damage when signaling to end the capture
            // should trigger the necessary final frame.
            //
            // TODO: Go through uses of capturing_video to check for redundancy now
            // that we have a global state, with e.g. `.exit_requested`.
            if (output->capture.video_end_requested || g_state.exit_requested) {
                end_video_capture(output);
            } else {
                // TODO: avio_flush ?
                struct ext_image_copy_capture_frame_v1 *next_frame = video_capture_create_frame(&output->capture);
                ext_image_copy_capture_frame_v1_capture(next_frame);
                output->capture.session.frame_ctx.frame = next_frame;
            }
        }
    }

    frame_ctx->capture_buffer_damage_area_px = (BLBoxI){0};
}


static void
handle_image_copy_capture_frame_failed(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t reason
) {
    ext_image_copy_capture_frame_v1_destroy(frame);

    struct capture_frame_context *frame_ctx = data;
    struct scran_output          *output    = frame_ctx->output;
    frame_ctx->frame = NULL;

    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME) {
        eprintf("ERROR: freezeframe capture failed (%u)\n", reason);
        // FIXME: Handle this better?
        continue_after_showing_freezeframe(output);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE) {
        end_image_capture(output);
    }
    if (frame_ctx->consumers & SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO) {
        // TODO: Retry a few times?
        end_video_capture(output);
    }
}


struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener = {
    .transform = handle_image_copy_capture_frame_transform,
    .damage = handle_image_copy_capture_frame_damage,
    .presentation_time = handle_image_copy_capture_frame_presentation_time,
    .ready = handle_image_copy_capture_frame_ready,
    .failed = handle_image_copy_capture_frame_failed,
};
