#ifndef STATE_H
#define STATE_H

#include <stdbool.h>
#include <sys/uio.h>

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

#define MAX_OUTPUTS 64

// TODO: Buffer file/header?
#define A_DOUBLE_BUFFER_HAS_TWO_BUFFERS 2
#define SURFACE_BUF_COUNT A_DOUBLE_BUFFER_HAS_TWO_BUFFERS

struct client_state_globals {
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
};

struct client_state_output_surface_buffer {
    // TODO: Rename to wl_buffer to not mix it up with `data`?
    struct wl_buffer *buffer;
    void *data;
    bool busy;
    BLImageCore bl_img;
};

// TODO: Optimize surface/selection event-loop struct sizes
//           Make a *_context struct, like for capture_frame
struct client_state_output_surface {
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    struct client_state_output_surface_buffer double_buffer[SURFACE_BUF_COUNT];

    bool is_focused;
};

struct client_state_seat_pointer {
    struct wl_pointer *pointer;
    struct wp_cursor_shape_device_v1 *cursor_shape_device;

    // TODO: Should this be for the entire seat, and not just pointer?
    struct client_state_output *focused_output;

    enum wl_pointer_button_state btn_left_state;
    int x_px;
    int y_px;
};

struct client_state_seat_keyboard {
    struct wl_keyboard *keyboard;

    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
};

// TODO: Rename client_state_* objects to st_* ?
//          Or name the wl_* objects wl_* ?

struct client_state_seat {
    struct client_state_seat_pointer pointer;
    struct client_state_seat_keyboard keyboard;
    // TODO: struct wl_touch *touch;

    // TODO: Create bitfield? Why isn't that part of the library?
    uint32_t capabilities; // bitmask: enum wl_seat_capability
};

struct client_state_output_selection_blend2d {
    // TODO Drop the blend2d struct and just create handler context structs
    BLContextCore ctx;
    BLPathCore path;

    // TODO Rename to box_px OR remove _px suffix from everything in state,
    //      now that all of state should have been standardized to pixel
    //      integer values (same for other boxes, resize_origin_pointer, etc.)
    struct BLBoxI box;
    struct BLBoxI box_outer;

    // TODO: Clearer name? This should be used to store the pre-resize/rebase box
    struct BLBoxI box_before_changes;
};

enum selection_state {
    // TODO: Bitmask and allow simultaneous f.ex. rebasing + resizing?
    SELECTION_EXIT_REQUESTED = -1,
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

// TODO: Optimize surface/selection event-loop struct sizes
//           Make a *_context struct, like for capture_frame
struct client_state_output_selection {
    // bool selection_started;
    enum selection_state selection_state;
    enum selection_resize_direction selection_resize_direction;

    // TODO: Make a cleaner/more obvious interface for getting selection
    //       height/width etc. than just getting the .bl.box coordinates?
    struct client_state_output_selection_blend2d bl;

    int pointer_before_changes_x_px;
    int pointer_before_changes_y_px;

    // TODO: Not needed? Just use box only?
    // BLPoint bl_point_top_left;
    // BLPoint bl_point_bottom_right;
};

// TODO: Merge all or parts of this with client_state_surface_buffer?
struct client_state_capture_buffer {
    struct wl_buffer *buffer;
    void *data;
};

// TODO: More consistent naming?
struct capture_frame_context {
    struct client_state_capture_buffer st_buffer;
    void *img_data_2;

    struct ext_image_copy_capture_session_v1 **session;

    AVFormatContext *av_format_ctx;
    AVCodecContext *av_codec_ctx;
    AVFrame *av_frame_encoded;
    SwsContext *sws_ctx;

    // TODO: Maybe union with libav or a separate frame_ctx or similar
    BLImageCore bl_img_captured;
    BLPixelConverterCore bl_pixel_converter;
    BLImageCodecCore bl_img_codec;

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
    bool capturing;
};

struct client_state_output_capture {
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

struct client_state_output_mode {
    // NOTE: These do not have any transforms applied
    //       (but they are affected by resolution settings)
    int32_t width_px;
    int32_t height_px;
    int32_t refresh_rate_mHz;
};

struct client_state_output {
    struct wl_output *wl_output;
    // TODO: xdg_output
    //         will at least be needed if/when implementing f.ex.
    //         cross-output capture, or other features requiring awareness of
    //         global geometry

    // TODO: Clearer name ?
    struct client_state_output_mode mode;
    enum wl_output_transform transform;

    struct client_state_output_surface surface;
    struct client_state_output_selection selection;
    struct client_state_output_capture capture;
};

struct client_state {
    struct client_state_globals globals;
    struct client_state_seat seat;
    // TODO: Pointers, probably. This entire state mess still needs cleaning up in general.
    struct client_state_output outputs[MAX_OUTPUTS];
    uint32_t n_outputs;

    bool exit_requested;
};

#endif
