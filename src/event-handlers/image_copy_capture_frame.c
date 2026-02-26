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
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <stdatomic.h>

#include "ext-image-copy-capture-v1.h"
#include "ext-data-control-v1.h"

#include "libavutil/avutil.h"
#include "state.h"
#include "event-handlers.h"
#include "capture.h"
#include "print.h"
#include "util/blend2d.h"
#include "init.h"
#include "simd.h"
#include "state-util.h"
#include "lib_interop.h"

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

static inline void
_write_video_frame(
    struct capture_frame_context *frame_ctx,
    AVPacket *av_packet // Encoded frame
) {
    assert(av_packet != NULL);

    const AVStream *const _av_stream = frame_ctx->av_format_ctx->streams[AV_FORMAT_STREAM_IDX_VIDEO];

    av_packet->stream_index = AV_FORMAT_STREAM_IDX_VIDEO;
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

// TODO:
//  - Can we fully avoid capturing the overlay (beyond just 
//    making sure it's out of frame) ?
//  - Either assert width/height isn't 0 (and enforce in selection logic)
//    or handle it properly here
//  - Allow resizing capture frame during recording
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

    // TODO: Find a way to clean this up and make it prettier

    // TODO: Clarify names, more in sync with start_capture names?
    const uint32_t source_row_bytes = frame_ctx->pixel_stride * frame_ctx->source_width_px;
    uint8_t *const area_start_addr =
        frame_ctx->st_buffer.data
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;


    // Convert

    int _retval_filter;
    // XXX: Can we make this const so area_start_addr can be const? It should
    // not change for the lifetime of this function (well, at least until the
    // next frame's dispatch at the end).
    frame_ctx->av_frame_captured->data[0] = area_start_addr;
    frame_ctx->av_frame_captured->pts = frame_ctx->presentation_time_nsec;

    _retval_filter = av_buffersrc_add_frame_flags(
            frame_ctx->av_filter_buffersrc_ctx,
            frame_ctx->av_frame_captured,
            // TODO: AV_BUFFERSRC_FLAG_KEEP_REF, but need safe setup, e.g.
            //       a ring buffer, so we know the frame doesn't get
            //       overwritten
            //           TODO: Check whether the graph can actually buffer
            //           frames in a way where this matters, beyond the
            //           life of this function (until next capture frame
            //           dispatch). In any case we will probably need a
            //           safe setup like this for the encoder.
            0
    );
    assert(0 <= _retval_filter);

    _retval_filter = av_buffersink_get_frame(
            frame_ctx->av_filter_buffersink_ctx,
            frame_ctx->av_frame_converted
    );
    assert(0 <= _retval_filter);


    // Encode

    av_frame_make_writable(frame_ctx->av_frame_converted);

    int _retval_enc = avcodec_send_frame(frame_ctx->av_codec_ctx, frame_ctx->av_frame_converted);
    assert(_retval_enc != AVERROR(EINVAL));
    // TODO: Initialize this once, and put in frame_ctx
    AVPacket *av_packet = av_packet_alloc();
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

        _write_video_frame(frame_ctx, av_packet);

        // INFO: packet gets unreferenced at start of loop by avcodec_receive_packet
    }

    // NOTE: We do this check *after* writing the incoming frame. This ensures
    // that the video will not be cut short at the end if we're only capturing
    // frames on demand (with variable framerate) and nothing has changed for
    // the last x amount of time.
    // Calling capture_frame::damage_buffer() when signaling to end the capture
    // should trigger the necessary final frame.
    //
    // TODO: Go through uses of capturing_video to check for redundancy now
    // that we have a global state, with e.g. `.exit_requested`.
    if (!frame_ctx->capturing_video) {
        goto end_capture;
    } else if (g_state.exit_requested) {
        frame_ctx->capturing_video = false;
        goto end_capture;
    }

    // TODO: Double-check that freeing is safe wrt. encoder interleaving etc.,
    //       or whether we should unref instead
    av_packet_free(&av_packet);

    // TODO: avio_flush ?

    init_wl_capture_frame__video(frame_ctx);
    ext_image_copy_capture_frame_v1_capture(frame_ctx->frame);

    return;

    // TODO: Probably define end_capture as an end_video_capture function, in the
    // same file as start_video_capture, to make setup/teardown less disjointed.
end_capture:
    // Drain codec
    avcodec_send_frame(frame_ctx->av_codec_ctx, NULL);
    assert(av_packet != NULL);
    while (avcodec_receive_packet(frame_ctx->av_codec_ctx, av_packet) != AVERROR_EOF) {
        _write_video_frame(frame_ctx, av_packet);
    }
    av_packet_free(&av_packet);

    // Finalize file
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
    assert(frame_ctx->av_frame_converted);
    av_frame_free(&frame_ctx->av_frame_converted);

    atomic_fetch_sub_explicit(&g_state.n_captures_in_progress, 1, memory_order_relaxed);

    struct scran_output_capture *const st_capture = wl_container_of(frame_ctx, st_capture, frame_ctx);
    struct scran_output *const st_output = wl_container_of(st_capture, st_output, capture);
    set_surface_theme(st_output, SURFACE_THEME_DEFAULT);

    DEBUG("FINISHED RECORDING.\n");
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

    BLResult res;

    // TODO: Clarify names, more in sync with start_capture names?
    const int area_width_no_transform = blboxi_width_abs_unsafe(frame_ctx->capture_area_px);
    const int area_height_no_transform = blboxi_height_abs_unsafe(frame_ctx->capture_area_px);
    const uint32_t area_row_bytes = frame_ctx->pixel_stride * area_width_no_transform;
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
        // XXX TODO(!!!): Create helper functions to convert from wl_shm_format
        // to our desired format here.
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

    // TODO: This should be called once, outside of the capture event pipeline,
    // unless between-capture format changing is implemented.
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
    const char _file_extension[] = CAPTURE_IMAGE_OUTPUT_FILE_EXTENSION_DEFAULT;
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
