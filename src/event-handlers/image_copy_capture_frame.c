#include <unistd.h>
#include <assert.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <libavcodec/codec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>

#include "blend2d/core/api.h"
#include "blend2d/core/imagecodec.h"
#include "blend2d/core/pixelconverter.h"
#include "ext-image-copy-capture-v1.h"

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
    const uint64_t sec_to_nsec = ((uint64_t)tv_sec_hi << 32 | tv_sec_lo) * NSEC_PER_SEC;

    frame_ctx->presentation_time_nsec = sec_to_nsec + tv_nsec;
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

    if (!frame_ctx->capturing) {
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
    struct client_state_output_capture *st_capture = data;
    struct capture_frame_context *frame_ctx = &st_capture->frame_ctx;

    // XXX: Not implemented yet...
    assert(!frame_ctx->capturing);

    ext_image_copy_capture_frame_v1_destroy(frame);


    // TODO: Clarify names, more in sync with start_capture names?
    const int area_width = blboxi_width_abs_unsafe(frame_ctx->capture_area_px);
    const int area_height = blboxi_height_abs_unsafe(frame_ctx->capture_area_px);
    const uint32_t source_row_bytes = frame_ctx->pixel_stride * frame_ctx->source_width_px;
    const uint32_t area_row_bytes = frame_ctx->pixel_stride * area_width;
    // const int capture_area_width_px = blboxi_width_abs_unsafe(frame_ctx->capture_area_px);
    const uint8_t *const area_start_addr =
        frame_ctx->st_buffer.data
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.y0 * frame_ctx->source_width_px
        + frame_ctx->pixel_stride * frame_ctx->capture_area_px.x0;


    // TODO: Remove or actually use...
    BLResult res;

    // XXX TODO: Ensure good defaults
    BLFormatInfo _bl_format_info_dst = bl_format_info[_FORMAT_PNG_BLEND2D_OUTPUT_FORMAT];
    BLFormatInfo _bl_format_info_src = wl_shm_format_to_blend2d_struct(st_capture->shm_format);

    if (_bl_format_info_src.depth == 0) {
        eprintf("Error: Unsupported format. Aborting image capture.\n");
        return;
    }

    // XXX: We just always run it through the converter for now.
    // TODO: Only convert if required (not natively supported pixel format by blend2d)
    //       *maybe* also reconsider using a different library.
    BLPixelConverterCore bl_pixel_converter;
    bl_pixel_converter_init(&bl_pixel_converter);
    res = bl_pixel_converter_create(
        &bl_pixel_converter,
        &_bl_format_info_dst,
        &_bl_format_info_src,
        ( BL_PIXEL_CONVERTER_CREATE_FLAG_DONT_COPY_PALETTE
        | BL_PIXEL_CONVERTER_CREATE_FLAG_ALTERABLE_PALETTE
        )
    );
    DEBUG("image_copy_capture_frame.c: bl_pixel_converter_create:  %d\n", res);

    void *const bl_buf_cropped_converted = frame_ctx->img_data_2;
    res = bl_pixel_converter_convert(
        &bl_pixel_converter,
        bl_buf_cropped_converted,
        area_row_bytes,
        area_start_addr,
        source_row_bytes,
        area_width,
        area_height,
        NULL
    );
    DEBUG("image_copy_capture_frame.c: bl_pixel_converter_convert:  %d\n", res);

    // TODO: Find out whether bl_*_init_as_* functions are efficient enough, or
    // whether there's some lower-overhead way of looping on adding new
    // data/width/height etc. that doesn't require full destruction/re-allocation
    res = bl_image_init_as_from_data(
        &frame_ctx->bl_img_captured,
        area_width,
        area_height,
        _FORMAT_PNG_BLEND2D_OUTPUT_FORMAT,
        bl_buf_cropped_converted,
        area_row_bytes,
        BL_DATA_ACCESS_READ,
        NULL,
        NULL
    );
    DEBUG("image_copy_capture_frame.c: bl_image_init_as_from_data:  %d\n", res);

    BLImageCodecCore bl_img_codec;
    res = bl_image_codec_init_by_name(&bl_img_codec, _FORMAT_PNG_BLEND2D_CODEC_NAME, SIZE_MAX, NULL);

    char filename[NAME_MAX];
    create_timestamped_filename(filename, _FORMAT_PNG_FILE_EXTENSION);
    res = bl_image_write_to_file(&frame_ctx->bl_img_captured, filename, &bl_img_codec);

    if (res == BL_SUCCESS) {
        eprintf("Image saved to %s\n", filename);
    } else {
        eprintf("Error: Failed to save image (attempted: %s).", filename);
    }

    // TODO: Double-check whether this order is fine.
    bl_pixel_converter_destroy(&bl_pixel_converter);
    bl_image_codec_destroy(&bl_img_codec);
    bl_image_destroy(&frame_ctx->bl_img_captured);
}



struct ext_image_copy_capture_frame_v1_listener image_copy_capture_frame_listener__image_capture = {
    .transform = handle_image_copy_capture_frame_transform__image_capture,
    .damage = handle_image_copy_capture_frame_damage__image_capture,
    .presentation_time = handle_image_copy_capture_frame_presentation_time__image_capture,
    .ready = handle_image_copy_capture_frame_ready__image_capture,
};
