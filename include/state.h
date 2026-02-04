#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include <sys/uio.h>
#include <stdatomic.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <xkbcommon/xkbcommon.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "cursor-shape-v1.h"
#include "xdg-output-unstable-v1.h"
#include "ext-data-control-v1.h"

#define MAX_OUTPUTS 64

// TODO: Buffer file/header?
#define A_DOUBLE_BUFFER_HAS_TWO_BUFFERS 2
#define SURFACE_BUF_COUNT A_DOUBLE_BUFFER_HAS_TWO_BUFFERS

// TODO: Move these definitions elsewhere?
#define BLCONTEXT_RGBA32_FILL_STYLE_DEFAULT ((struct BLRgba32){ 0x88888888 })
#define BLCONTEXT_RGBA32_FILL_STYLE_VIDEO_CAPTURE ((struct BLRgba32){ 0x88887A7A })

struct scran_globals {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    struct ext_output_image_capture_source_manager_v1 *output_image_capture_source_manager;
    struct ext_image_copy_capture_manager_v1 *image_copy_capture_manager;
    struct ext_data_control_manager_v1 *data_control_manager;
};

struct scran_output_surface_buffer {
    struct wl_buffer *wl_buffer;
    // TODO: Make sure frame data (here and in capture) gets proper alignment.
    void *data;

    BLImageCore bl_img;
    BLBoxI bl_box_rendered;

    bool busy;
};

// TODO: Optimize surface/selection event-loop struct sizes
//           Make a *_context struct, like for capture_frame
struct scran_output_surface {
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    struct scran_output_surface_buffer double_buffer[SURFACE_BUF_COUNT];
};

struct scran_seat_pointerContext {
    int x_px;
    int y_px;

    // TODO: Use this for click-and-hold or remove it. Also probably change it
    // to a bool
    enum wl_pointer_button_state btn_left_state;

    // TODO: Should this be for the entire seat, and not just pointer?
    //           NOTE: Both keyboard and pointer have ::enter events.
    struct scran_output *focused_output;

    struct wp_cursor_shape_device_v1 *cursor_shape_device;
};

struct scran_seat_keyboard {
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
};

// XXX: Rename this?
struct scran_seat_datacontrol {
    // TODO: Avoid double-dereference (change double-derefs in other state
    // structs as well)
    struct ext_data_control_manager_v1 **manager;
    struct ext_data_control_device_v1 *device;
    struct ext_data_control_source_v1 *source;
    // TODO: Get data from save-path

    // NOTE: Handed over from ::frame event. Remember to destroy/unref on
    // data_control::cancelled (or if overwriting pointer), if required based
    // on ::frame (required at time of writing).
    // TODO: Check whether a BLImage maintains a reference to the BLCoded (and,
    // by extension, the mimetype string), and consider just handing over the
    // entire BLImage instead. Maybe doesn't make sense to do even then, though.
    BLArrayCore data_to_send;
    // TODO: Allow multiple mimetypes?
    const char *data_to_send_mime_type;

    bool selection_active;
};

struct scran_seat {
    struct scran_seat_pointerContext pointer_ctx;
    struct scran_seat_keyboard keyboard;
    struct scran_seat_datacontrol datacontrol;

    struct wl_pointer *wl_pointer;
    struct wl_keyboard *wl_keyboard;
    // TODO: struct wl_touch *touch;
};

enum selection_state {
    // TODO: Bitmask and allow simultaneous f.ex. rebasing + resizing?
    SELECTION_NONE,
    SELECTION_IN_PROGRESS,
    SELECTION_COMPLETE,
    SELECTION_REBASING,
    SELECTION_RESIZING,
};

enum selection_resize_direction {
    SELECTION_RESIZE_NONE,
    SELECTION_RESIZE_TOP_LEFT,
    SELECTION_RESIZE_TOP_RIGHT,
    SELECTION_RESIZE_BOTTOM_LEFT,
    SELECTION_RESIZE_BOTTOM_RIGHT,
};

// This struct is used as a shared context struct for event handlers that
// need to interact with the selection (capture area) state. Keep frequently
// accessed members near the start.
struct scran_output_selectionContext {
    BLContextCore bl_ctx;
    // TODO: Either drop this as a member or actually retain the path state
    // between redraws.
    BLPathCore bl_path;
    // TODO Rename to box_px OR remove _px suffix from everything in state,
    //      now that all of state should have been standardized to pixel
    //      integer values (same for other boxes, resize_origin_pointer, etc.)
    // NOTE: This is allowed to be inverted to make resizing simpler.
    struct BLBoxI bl_box;
    // TODO: This doesn't really need to be a state variable. Make a macro or
    // something to calculate it inline to match output width/height and x=y=0.
    struct BLBoxI bl_box_outer;

    enum selection_state selection_state;

    // TODO: Clearer name? This should be used to store the pre-resize/rebase box
    struct BLBoxI bl_box_before_changes;
    enum selection_resize_direction selection_resize_direction;
    int pointer_before_changes_x_px;
    int pointer_before_changes_y_px;
};

struct scran_capture_buffer {
    struct wl_buffer *wl_buffer;
    void *data;
};

// TODO: More consistent naming?
// TODO: Separate video/image capture context
// TODO: We don't need pointers to parent struct members. Use offsetof or wl_container_of.
struct capture_frame_context {
    struct scran_capture_buffer st_buffer;
    void *img_data_2;

    // TODO: This entire frame context badly needs re-reorganizing and slimming
    struct ext_image_copy_capture_session_v1 **session;
    struct scran_seat_datacontrol *st_datacontrol;

    AVFormatContext *av_format_ctx;
    AVCodecContext *av_codec_ctx;
    AVFrame *av_frame_encoded;
    SwsContext *sws_ctx;

    // TODO: Maybe union with libav or a separate frame_ctx or similar
    BLImageCore bl_img_captured;
    BLPixelConverterCore bl_pixel_converter;
    BLImageCodecCore bl_imgcodec;

    uint64_t presentation_time_nsec;

    //  NOTE: Capture area should be set synchronously with the drawn overlay's
    //        area (or be set based on the same real-time values). Otherwise,
    //        its graphics can spill into the capture frame.
    //        F.ex., the mouse can have moved in-between overlay's frame draw
    //        and capture's frame "draw".
    //        TODO: Double-check whether anything else should be synced like this.
    struct BLBoxI capture_area_px; // NOTE: Transform should be reversed.
    uint32_t pixel_stride;
    // TODO: Get this through output.mode if we both end up pointing to it here,
    //       AND it is still asserted to be equal to session::buffer_size's
    //       width arg.
    int32_t source_width_px;

    // TODO: Probably turn this into a union with some member that gets
    //       re-initialized with every start/stop capture.
    //         - Union with presentation_time ?
    bool capturing_video;
};

struct scran_output_capture {
    struct capture_frame_context frame_ctx;

    // Inconsistent naming, but my eyes are bleeding
    struct ext_image_capture_source_v1 *source;
    struct ext_image_copy_capture_session_v1 *session;

    // TODO: Probably put this into a separate struct. Mode?
    //       Something to separate it from both capture/output and from xdg output
    // NOTE: These do not have any transforms applied.
    //       Capture frame buffer must match this size and handle transforms
    //       manually.
    uint32_t shm_format;
};

struct scran_output_mode {
    // NOTE: These do not have any transforms applied
    //       (but they are affected by resolution settings)
    int32_t width_px;
    int32_t height_px;
    int32_t refresh_rate_mHz;
};

struct scran_output {
    struct wl_output *wl_output;
    // TODO: xdg_output
    //         will at least be needed if/when implementing f.ex.
    //         cross-output capture, or other features requiring awareness of
    //         global geometry

    // TODO: Clearer name ?
    struct scran_output_mode mode;
    enum wl_output_transform transform;

    struct scran_output_surface surface;
    struct scran_output_selectionContext selection_ctx;
    struct scran_output_capture capture;
};

struct scran {
    // TODO: Make this a state enum or a bitfield with datacontrol.selection_active etc. ?
    bool exit_requested;
    // NOTE: Consider a custom wl_event_queue if this for some reason ends up
    // becoming convoluted in the future.
    //     TODO: Consider doing this already so we can easily and safely
    //           short-circuit all the other event loops while we block on the
    //           capture-related ones.
    // TODO: Consider separate for video vs image, for better asserts, if
    // nothing else.
    atomic_int n_captures_in_progress;

    struct scran_globals globals;
    struct scran_seat seat;

    uint32_t n_outputs;
    struct scran_output outputs[MAX_OUTPUTS];
};

#endif
