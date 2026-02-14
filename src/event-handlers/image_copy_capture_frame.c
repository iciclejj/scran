#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <stdatomic.h>

#include "blend2d/core/api.h"
#include "blend2d/core/array.h"
#include "blend2d/core/imagecodec.h"
#include "blend2d/core/pixelconverter.h"
#include "ext-image-copy-capture-v1.h"
#include "ext-data-control-v1.h"

#include "libavutil/avutil.h"
#include "state.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "util/blend2d.h"
#include "lib_interop.h"

#define _FORMAT_PNG_FILE_EXTENSION ".png"
#define _FORMAT_PNG_BLEND2D_CODEC_NAME "PNG"
#define _FORMAT_PNG_BLEND2D_OUTPUT_FORMAT BL_FORMAT_PRGB32 // pixel format

extern struct scran g_state;

static void
handle_image_copy_capture_frame_transform__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t transform
) {
    struct capture_frame_context *frame_ctx = data;

    // TODO: What is this transform representing?
    //           It is separate from output::geometry's transform.
}


static void
handle_image_copy_capture_frame_damage__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height
) {
    struct capture_frame_context *frame_ctx = data;

    // XXX TODO IMPORTANT: Implement this and add flag to enable damage-based capture
}

static void
handle_image_copy_capture_frame_presentation_time__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    struct capture_frame_context *frame_ctx = data;

    // XXX: Will overflow at tv_sec > ~584.9 years...
    const uint64_t tv_sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = tv_sec_to_nsec + tv_nsec;
}

// TODO:
//  - Can we fully avoid capturing the overlay (beyond just 
//    making sure it's out of frame) ?
//  - Either assert width/height isn't 0 (and enforce in selection logic)
//    or handle it properly here
//  - Allow resizing capture frame during recording
//     - Should be built around sws_scale and/or encoder filters,
//       whichever will be in use for cropping and scaling whenever
//       resizing gets implemented.
//
//  - Perform libav allocations in meminit
//       NOTE: Read up on the libav refcount mechanisms first.
//       https://www.ffmpeg.org/doxygen/trunk/group__lavc__encdec.html
//
static void
handle_image_copy_capture_frame_ready__video_capture(
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame
) {
    struct capture_frame_context *frame_ctx = data;

    ext_image_copy_capture_frame_v1_destroy(frame);

    // TODO: Go through uses of capturing_video to check for redundancy now
    // that we have a global state, with e.g. `.exit_requested`.
    if (!frame_ctx->capturing_video) {
        goto end_capture;
    } else if (g_state.exit_requested) {
        frame_ctx->capturing_video = false;
        goto end_capture;
    }

    // TODO: Find a way to clean this up and make it prettier

    // TODO: Clarify names, more in sync with start_capture names?
    const uint32_t source_row_bytes = frame_ctx->pixel_stride * frame_ctx->source_width_px;
    const uint8_t *const area_start_addr =
        frame_ctx->st_buffer.data
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;

    // XXX: We won't ever get planar src frame buffers... right..?
    const uint8_t *const _captured_planebufs[1] = { area_start_addr };
    const int _captured_linesizes[1] = { (int)source_row_bytes };
    // TODO: Is this necessary? Can we manage refs etc. more efficiently manually?
    av_frame_make_writable(frame_ctx->av_frame_encoded);
    // INFO: Width set during sws_ctx's init ensures crop
    // XXX: This sws_scale cropping "hack" needs to be updated to allow
    // non-equal length planes (e.g. YUV420).
    //     TODO: Update it. Important for compatibility and cheaper decode.
    assert(frame_ctx->sws_ctx->src_w == blboxi_width_abs_unsafe(frame_ctx->capture_area_px));
    sws_scale(
        frame_ctx->sws_ctx,
        _captured_planebufs, _captured_linesizes, 0, blboxi_height_abs_unsafe(frame_ctx->capture_area_px),
        frame_ctx->av_frame_encoded->data, frame_ctx->av_frame_encoded->linesize
    );
    assert(frame_ctx->av_codec_ctx->time_base.den == NSEC_PER_SEC);
    frame_ctx->av_frame_encoded->pts = frame_ctx->presentation_time_nsec;

    int _retval_enc = avcodec_send_frame(frame_ctx->av_codec_ctx, frame_ctx->av_frame_encoded);
    assert(_retval_enc != AVERROR(EINVAL));
    AVPacket *av_packet = av_packet_alloc(); // XXX: Redundant with av_new_packet? And maybe vise-versa in our case?
    while (_retval_enc >= 0) {
        _retval_enc = avcodec_receive_packet(frame_ctx->av_codec_ctx, av_packet);
        assert(_retval_enc != AVERROR(EINVAL));

        // TODO: Are there other > 0 error codes it could return?
        if (_retval_enc == AVERROR_EOF || _retval_enc == AVERROR(EAGAIN)) {
            break;
        } else if (_retval_enc < 0) {
            eprintf("Error while encoding frame\n");

            // TODO: goto err
            return;
        }

        av_packet->stream_index = AV_FORMAT_STREAM_IDX_VIDEO;
        const AVStream *const _av_stream = frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO];
        assert(av_packet->stream_index == _av_stream->index);
        // NOTE: This is doing the work of av_packet_rescale_ts(), but with
        // asserts instead of conditionals. Just switch to that or to
        // if-statements if indeterminism becomes necessary.
        assert(av_packet->pts != AV_NOPTS_VALUE);
        av_packet->pts = av_rescale_q(av_packet->pts, frame_ctx->av_codec_ctx->time_base, _av_stream->time_base);
        assert(av_packet->dts != AV_NOPTS_VALUE);
        av_packet->dts = av_rescale_q(av_packet->dts, frame_ctx->av_codec_ctx->time_base, _av_stream->time_base);
        assert(av_packet->duration <= 0);

        // TODO: Look into conditionally using av_write_frame for sequential
        //       encoding
        av_interleaved_write_frame(frame_ctx->av_format_ctx, av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    // TODO: Double-check that freeing is safe wrt. encoder interleaving etc.,
    //       or whether we should unref instead
    av_packet_free(&av_packet);

    // TODO: avio_flush ?

    dispatch_video_capture_event_loop(frame_ctx);

    return;

end_capture:
    av_write_trailer(frame_ctx->av_format_ctx);
end_capture_err:
    // Note: Most (all?) of these are fine to call with null pointers, despite
    // the asserts
    assert(frame_ctx->av_format_ctx->pb);
    avio_close(frame_ctx->av_format_ctx->pb);
    assert(frame_ctx->av_format_ctx);
    avformat_free_context(frame_ctx->av_format_ctx);
    assert(frame_ctx->av_codec_ctx);
    avcodec_free_context(&frame_ctx->av_codec_ctx);
    assert(frame_ctx->av_frame_encoded);
    av_frame_free(&frame_ctx->av_frame_encoded);

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
    return;
}


// TODO: Maybe find some nicer naming convention than __video_capture etc., idk
struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__video_capture = {
    .transform = handle_image_copy_capture_frame_transform__video_capture,
    .damage = handle_image_copy_capture_frame_damage__video_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__video_capture,
    .ready = handle_image_copy_capture_frame_ready__video_capture,
};


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

    // TODO: Remove or actually use...
    BLResult res;

    // XXX TODO: Ensure good defaults
    BLFormatInfo bl_format_info_src = wl_shm_format_to_blend2d_struct(st_capture->shm_format);
    BLFormatInfo bl_format_info_dst = bl_format_info[_FORMAT_PNG_BLEND2D_OUTPUT_FORMAT];

    if (bl_format_info_src.depth == 0) {
        eprintf("Error: Unsupported format. Aborting image capture.\n");
        return;
    }

    // XXX: We just always run it through the converter for now.
    // TODO: Only convert if required (not natively supported pixel format by blend2d)
    //       *maybe* also reconsider using a different library.
    //           Unless blend2d does that on its own. Find out.
    res = bl_pixel_converter_create(
        &frame_ctx->bl_pixel_converter,
        &bl_format_info_dst,
        &bl_format_info_src,
        ( BL_PIXEL_CONVERTER_CREATE_FLAG_DONT_COPY_PALETTE
        | BL_PIXEL_CONVERTER_CREATE_FLAG_ALTERABLE_PALETTE
        )
    );
    DEBUG("image_copy_capture_frame.c: bl_pixel_converter_create:  %d\n", res);

    // TODO: Clarify names, more in sync with start_capture names?
    const int area_width = blboxi_width_abs_unsafe(frame_ctx->capture_area_px);
    const int area_height = blboxi_height_abs_unsafe(frame_ctx->capture_area_px);
    const uint32_t area_row_bytes = frame_ctx->pixel_stride * area_width;
    const uint32_t source_row_bytes = frame_ctx->pixel_stride * frame_ctx->source_width_px;
    // XXX TODO: Either eparate buffer from video capture OR double-check that
    // the shared buffer doesn't cause issues + add robust checks/asserts
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
    res = bl_pixel_converter_convert(
        &frame_ctx->bl_pixel_converter,
        bl_buf_cropped_converted,
        area_row_bytes,
        area_start_addr,
        source_row_bytes,
        area_width,
        area_height,
        NULL
    );
    DEBUG("image_copy_capture_frame.c: bl_pixel_converter_convert:  %d\n", res);

    // NOTE: The data passed is not freed unless freed by passed destroy_func,
    // if it is not NULL (aka it is not freed here, at time of writing).
    res = bl_image_create_from_data(
        &frame_ctx->bl_img_captured,
        area_width,
        area_height,
        _FORMAT_PNG_BLEND2D_OUTPUT_FORMAT,
        bl_buf_cropped_converted,
        area_row_bytes,
        // XXX: Read-only access causes blend2d to make a copy if modified.
        // TODO: Probably just change to RW.
        BL_DATA_ACCESS_READ,
        NULL,
        NULL
    );
    DEBUG("image_copy_capture_frame.c: bl_image_init_as_from_data:  %d\n", res);

    // TODO: This should be called once, outside of the capture event pipeline,
    // unless between-capture format changing is implemented.
    res = bl_image_codec_find_by_name(&frame_ctx->bl_imgcodec, _FORMAT_PNG_BLEND2D_CODEC_NAME, SIZE_MAX, NULL);

    // TODO: This should be initialized in init_premem, so we don't re-allocate
    // the array backing every time. Must in that case either be a double-
    // buffer, OR assert that there will never be a race condition with
    // offer::send.
    //     Probably simply doing everything within one run of this function
    //     should to be enough, assuming no multi-threading?
    BLArrayCore bl_array_img_encoded;
    bl_array_init(&bl_array_img_encoded, BL_OBJECT_TYPE_ARRAY_UINT8);
    res = bl_image_write_to_data(&frame_ctx->bl_img_captured, &bl_array_img_encoded, &frame_ctx->bl_imgcodec);

    // TODO: Conditional save to file and/or to clipboard selection

    // XXX: Everything here is so bad... Should really switch to a more
    // c-friendly library...
    //     TODO: Are the internal functions reasonably stable and/or easy to
    //     use directly?
    const void *const bl_array_img_encoded_data = bl_array_get_data(&bl_array_img_encoded);
    const size_t bytes_to_write = bl_array_get_size(&bl_array_img_encoded);
    size_t bytes_written;

    // XXX TODO: Refactor path-related things once we implement custom save-path
    // arg-parsing. Keep everything contained here until then, despite being
    // inefficient. Also needs better error handling etc.
    char filepath[PATH_MAX] = CAPTURE_OUTPUT_DEFAULT_DIRPATH "/";
    mkdir(filepath, 0755);
    const size_t _filename_offset = sizeof(CAPTURE_OUTPUT_DEFAULT_DIRPATH);
    const char _file_extension[] = _FORMAT_PNG_FILE_EXTENSION;
    create_timestamped_filename(filepath + _filename_offset, _file_extension);
    res = bl_file_system_write_file(
        filepath,
        bl_array_img_encoded_data,
        bytes_to_write,
        &bytes_written
    );

    if (res == BL_SUCCESS && bytes_written == bytes_to_write) {
        eprintf("Image saved: %s (%ldKiB)\n", filepath, bytes_written >> 10);
    } else {
        eprintf("Error: Failed to save image (attempted: %s).", filepath);
    }


    // TODO: Consider loading image back from storage, instead of storing it
    // in memory?

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

    // XXX: Calling this directly from here makes sense for now, since we only
    // support one seat. TODO: Make a function to decide which seat's
    // data_control_device should handle this.
    if (frame_ctx->st_datacontrol->source != NULL) {
        ext_data_control_source_v1_destroy(frame_ctx->st_datacontrol->source);
    }
    frame_ctx->st_datacontrol->source = ext_data_control_manager_v1_create_data_source(
        *frame_ctx->st_datacontrol->manager
    );
    ext_data_control_source_v1_add_listener(
        frame_ctx->st_datacontrol->source,
        &data_control_source_listener,
        frame_ctx->st_datacontrol
    );
    ext_data_control_source_v1_offer(
        frame_ctx->st_datacontrol->source,
        frame_ctx->st_datacontrol->data_to_send_mime_type
    );
    ext_data_control_device_v1_set_selection(frame_ctx->st_datacontrol->device, frame_ctx->st_datacontrol->source);
    DEBUG("image_capture::frame(): datacontrol_source::set_selection().\n");

    frame_ctx->st_datacontrol->selection_active = true;
    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);
}



struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__image_capture = {
    .transform = handle_image_copy_capture_frame_transform__image_capture,
    .damage = handle_image_copy_capture_frame_damage__image_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__image_capture,
    .ready = handle_image_copy_capture_frame_ready__image_capture,
};
