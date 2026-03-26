#ifndef STATE_H
#define STATE_H

#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/uio.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <xkbcommon/xkbcommon.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "cursor-shape-v1.h"
#include "xdg-output-unstable-v1.h"
#include "ext-data-control-v1.h"
#include "presentation-time.h"

#define MAX_OUTPUTS 64

#define A_DOUBLE_BUFFER_HAS_TWO_BUFFERS 2
#define SURFACE_BUF_COUNT A_DOUBLE_BUFFER_HAS_TWO_BUFFERS

// TODO: Move these definitions elsewhere?
#define BLCONTEXT_RGBA32_FILL_STYLE_DEFAULT          ((struct BLRgba32){ 0x880E0E0E })
#define BLCONTEXT_RGBA32_FILL_STYLE_VIDEO_CAPTURE    ((struct BLRgba32){ 0x880E0E0E })

#define BLCONTEXT_RGBA32_STROKE_STYLE_DEFAULT        ((struct BLRgba32){ 0xE0FFFFFF })
#define BLCONTEXT_RGBA32_STROKE_STYLE_VIDEO_CAPTURE  ((struct BLRgba32){ 0xFFFF0000 })
#define BLCONTEXT_STROKE_WIDTH (1.5)
// Half of blend2d stroke "outline" goes inwards, half goes outwards
#define BLCONTEXT_STROKE_RADIUS (BLCONTEXT_STROKE_WIDTH / 2)

#define SCRAN_OUTPUT_FILENAME_SIZE_MAX    (NAME_MAX)                                       // Null terminator is  counted
#define SCRAN_OUTPUT_FILENAME_STRLEN_MAX  (NAME_MAX - 1)                                   // Null terminator not counted
// TODO: Allow longer dir path if filename is short enough..?
#define SCRAN_OUTPUT_DIRPATH_SIZE_MAX     (PATH_MAX - SCRAN_OUTPUT_FILENAME_SIZE_MAX)      // Null terminator is  counted
#define SCRAN_OUTPUT_DIRPATH_STRLEN_MAX   (PATH_MAX - SCRAN_OUTPUT_FILENAME_SIZE_MAX - 1)  // Null terminator not counted
#define SCRAN_OUTPUT_FILEPATH_SIZE_MAX    (PATH_MAX)                                       // Null terminator is  counted
#define SCRAN_OUTPUT_FILEPATH_STRLEN_MAX  (PATH_MAX - 1)                                   // Null terminator not counted
// XXX: Semi-arbitrary value (highest built-in AVCodecDescriptor.name in
// libavcodec atm. is 18, excl. null-terminator).
#define SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX   20
#define SCRAN_OUTPUT_FILE_EXTENSION_STRLEN_MAX 20 - 1
#define SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX   (SCRAN_OUTPUT_FILENAME_SIZE_MAX - SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX)
#define SCRAN_OUTPUT_FILENAME_FORMATSTRING_STRLEN_MAX (SCRAN_OUTPUT_FILENAME_SIZE_MAX - SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX - 1)

#define SCRAN_MIME_TYPE_SIZE_MAX 256

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
    struct wp_presentation *presentation;
};

struct scran_output_surface_buffer {
    struct wl_buffer *wl_buffer;
    void *data;

    BLContextCore bl_ctx;
    BLImageCore bl_img;
    BLBoxI box_currently_drawn;

    bool busy;
};

// TODO: Optimize surface/selection event-loop struct sizes
//           Make a *_context struct, like for capture_frame
struct scran_output_surface {
    struct wl_surface *wl_surface;

    // TODO: Either drop this as a member or actually retain the path state
    // between redraws.
    BLPathCore bl_path;

    // XXX TODO: Turn this into a pointer once we remove the ugly redraw hack
    // in set_selection_surface_theme()
    BLBoxI box_last_drawn;
    struct scran_output_surface_buffer double_buffer[SURFACE_BUF_COUNT];

    struct zwlr_layer_surface_v1 *layer_surface;
};

struct scran_seat_pointerContext {
    int x_px;
    int y_px;

    // We only handle one button at a time
    // KEY_MAX is 2ff, but pointer::button sends uint32_t, so let's use that for now
    //     See: linux/input_event_codes.h.
    uint32_t active_button;
    // Only use press events; ignore release events.
    //     I.e. no button holding, and presses toggle the actions on/off
    //     (actions like rebasing, resizing, etc.).
    bool use_presses_only;

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
// TODO Isolate from rest of state.
struct scran_seat_datacontrol {
    struct ext_data_control_device_v1 *device;

    // We use blend2d types here for simpler interop with our image pipeline

    // NOTE: Handed over from ::frame event. Remember to destroy/unref on
    // data_control::cancelled (or if overwriting pointer), if required based
    // on ::frame (required at time of writing).
    // TODO: Check whether a BLImage maintains a reference to the BLImageCodec
    // (and, by extension, the mimetype string), and consider just handing over
    // the entire BLImage instead. Maybe doesn't make sense to do even then,
    // though.
    BLArrayCore data_to_send;

    // XXX: We can only have one actual active selection at a time (per seat),
    // but we use a refcount, rather than a bool, to not need to care about
    // the order of creating new_source vs triggering old_source::cancelled.
    atomic_int selection_refcount;

    char data_to_send_mime_type[SCRAN_MIME_TYPE_SIZE_MAX];
    char data_to_send_saved_file_path[SCRAN_OUTPUT_FILEPATH_SIZE_MAX];
    size_t data_to_send_saved_file_path_strlen;

    bool should_offer_filepath;
    bool should_offer_data;
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
    SELECTION_NONE,
    SELECTION_INITIALIZING,
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
    // TODO Rename to box_px OR remove _px suffix from everything in state,
    //      now that all of state should have been standardized to pixel
    //      integer values (same for other boxes, resize_origin_pointer, etc.)
    // NOTE: This is allowed to be inverted during resizing.
    struct BLBoxI bl_box;
    // TODO: This doesn't really need to be a state variable. Make a macro or
    // something to calculate it inline to match output width/height and x=y=0.
    struct BLBoxI bl_box_bounds;

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
struct capture_frame_context {
    struct ext_image_copy_capture_frame_v1 *frame;

    struct scran_capture_buffer st_buffer;
    void *img_data_2; // Extra buffer for copying/intermediate operations

    struct ext_image_copy_capture_session_v1 *wl_capture_session;
    struct scran_seat_datacontrol *st_datacontrol;

    AVFormatContext *av_format_ctx;
    AVCodecContext *av_codec_ctx;
    AVFrame *av_frame_captured;
    AVFrame *av_frame_to_encode;
    AVPacket *av_packet; // encoded frame
    AVFilterGraph *av_filter_graph;
    AVFilterContext *av_filter_buffersrc_ctx;
    // TODO: Do we need to keep non-endpoint filters (like transpose) around
    // for freeing them, or are they automatically freed through the parent
    // graph's refcouning?
    AVFilterContext *av_filter_transpose_ctx;
    AVFilterContext *av_filter_buffersink_ctx;

    BLImageCore bl_img_captured;
    BLPixelConverterCore bl_pixel_converter;
    BLImageCodecCore bl_imgcodec;

    uint64_t presentation_time_nsec;

    //  NOTE: Capture area should be set synchronously with the drawn overlay's
    //        area (or be set based on the same real-time values). Otherwise,
    //        its graphics can spill into the capture frame.
    //        F.ex., the mouse can have moved in-between overlay's frame draw
    //        and capture's frame "draw".
    struct BLBoxI capture_area_px; // NOTE: Transform should be reversed.
    // TODO: Get this through output.mode if we both end up pointing to it here,
    //       AND it is still asserted to be equal to session::buffer_size's
    //       width arg.
    int32_t source_width_px;
    uint8_t pixel_stride;

    // TODO: Probably turn this into a union with some member that gets
    //       re-initialized with every start/stop capture.
    //         - Union with presentation_time ?
    bool capturing_video;
};

struct scran_output_capture {
    struct capture_frame_context frame_ctx;

    struct ext_image_capture_source_v1 *source;

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

// Global logical geometry
struct scran_output_xdg_geometry {
    // NOTE: These DO have transforms and scale already applied.
    int32_t x_px;
    int32_t y_px;
    int32_t width_px;
    int32_t height_px;
};

struct scran_output {
    struct wl_output *wl_output;

    struct scran_output_mode mode;
    // Logical geometry, as given by the xdg_output protocol
    struct scran_output_xdg_geometry xdg_geometry;
    enum wl_output_transform transform;

    struct scran_output_surface surface;
    struct scran_output_selectionContext selection_ctx;
    struct scran_output_capture capture;

    // Only really needed during init and destruction:
    struct zxdg_output_v1 *xdg_output;
};

// TODO: Isolate this from scran state?
struct scran_options {
    char *output_path_filename_pointer;
    char output_path[SCRAN_OUTPUT_FILEPATH_SIZE_MAX]; // NOTE: Also used as output_directory during cli arg init
    char filename_format[SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX];
    bool output_to_stdout;

    bool no_keepalive;
    bool capture_and_exit_after_selection_init;
    bool produce_slurp;                 // output slurp-style geometry string
    bool have_custom_initial_selection; // output slurp-style geometry string
    struct BLRectI custom_initial_selection_global_coordinates;
    // bool dont_draw_selection;
};

// TODO: Struct alignments
struct scran {
    // TODO: Make this a state enum or a bitfield with datacontrol.selection_refcount etc. ?
    bool exit_requested;
    // NOTE: Consider a custom wl_event_queue if this for some reason ends up
    // becoming convoluted in the future.
    //     TODO: Consider doing this already so we can easily and safely
    //           short-circuit all the other event loops while we block on the
    //           capture-related ones.
    // TODO: Consider separate for video vs image, for better asserts, if
    // nothing else.
    // TODO: Atomic
    atomic_int n_captures_in_progress;

    struct scran_options options;

    struct scran_globals globals;
    struct scran_seat seat;

    // Used for releasing focus
    struct wl_region *empty_wl_region;
    sig_atomic_t sig_focus_requested;

    // TODO: Probably allocate this dynamically, after all.
    uint32_t n_outputs;
    struct scran_output outputs[MAX_OUTPUTS];
};

#endif
