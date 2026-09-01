#ifndef SCRAN_STATE_H
#define SCRAN_STATE_H

#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/uio.h>
#include <drm/drm_mode.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <blend2d/blend2d.h>
#include <xkbcommon/xkbcommon.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "cursor-shape-v1.h"
#include "xdg-output-unstable-v1.h"
#include "ext-data-control-v1.h"
#include "presentation-time.h"
#include "fractional-scale-v1.h"
#include "wlr-output-management-unstable-v1.h"
#include "cosmic-output-management-unstable-v1.h"

#include "compiler.h"
#include "ui.h"
#include "cursor.h"

#define MAX_OUTPUTS 64

#define A_DOUBLE_BUFFER_HAS_TWO_BUFFERS 2
#define SELECTION_SURFACE_BUF_COUNT A_DOUBLE_BUFFER_HAS_TWO_BUFFERS

#define SCRAN_OUTPUT_FILENAME_SIZE_MAX    (NAME_MAX)                                       // Null terminator is  counted
#define SCRAN_OUTPUT_FILENAME_STRLEN_MAX  (NAME_MAX - 1)                                   // Null terminator not counted
// TODO: Allow longer dirpath if filename is short enough..?
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

#define SCRAN_SHM_FORMAT_UNSET ((uint32_t)-1)

// NOTE: Output names are not actually guaranteed per spec to have this max
// length (nor for actual name to be equal to the underlying DRM name).
#define SCRAN_STATE_OUTPUT_NAME_SIZE DRM_CONNECTOR_NAME_LEN


extern struct scran g_state;


struct scran_globals {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct zxdg_output_manager_v1 *xdg_output_manager;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    struct ext_output_image_capture_source_manager_v1 *output_image_capture_source_manager;
    struct ext_image_copy_capture_manager_v1 *image_copy_capture_manager;
    struct ext_data_control_manager_v1 *data_control_manager;
    struct wp_presentation *presentation;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct zwlr_output_manager_v1 *wlr_output_manager;
    struct zcosmic_output_manager_v1 *cosmic_output_manager;
    struct wp_viewporter *viewporter;
    struct hyprland_surface_manager_v1 *hypr_surface_manager;
};

enum scran_stdout_reservation_purpose {
    SCRAN_STDOUT_RESERVATION_PURPOSE_NONE,
    SCRAN_STDOUT_RESERVATION_PURPOSE_IMAGE,
    SCRAN_STDOUT_RESERVATION_PURPOSE_VIDEO,
} SCRAN_PACKED;

struct scran_stdout_reservation {
    enum scran_stdout_reservation_purpose purpose;
};


struct scran_wl_buffer;
typedef void (*scran_wl_buffer_release_callback)(struct scran_wl_buffer *);

struct scran_wl_buffer {
    struct wl_buffer *wl_buffer;
    void *data;
    scran_wl_buffer_release_callback release_callback;
    bool busy;
};

struct scran_cursor_buffer {
    struct scran_wl_buffer scran_wl_buffer;
    BLImageCore bl_img;
};

struct scran_cursor {
    struct wl_surface *wl_surface;
    struct wp_viewport *viewport;
    struct scran_cursor_buffer buffers[SCRAN_CURSOR_N_THEMES];

    int width_height_px;
    enum scran_cursor_theme theme;
};

struct scran_output_subsurface {
    struct wl_surface *wl_surface;
    struct wl_subsurface *wl_subsurface;
    struct wp_viewport *viewport;

    int32_t width_px_buffer;
    int32_t height_px_buffer;
};

// TODO: Rename this to scran_output_layer_surface, and the _subsurface struct
// to _surface, and include it in the _layer_surface struct?
struct scran_output_surface {
    struct wl_surface *wl_surface;

    // "normalized" implies:
    //   scale == 1 => fractional_scale_factor_normalized = 1
    //   fractional_scale_wp_10 == 120 => fractional_scale_factor_normalized = 1
    //   fractional_scale_wp_10 == 180 => fractional_scale_factor_normalized = 1.5
    double final_scale_factor_normalized;

    // XXX: Everything below here should probably go in a separate init struct,
    // detached from the main state struct

    // Surface-local coordinates
    uint32_t width_logical;
    uint32_t height_logical;
    // Should (usually?) be equivalent to output_mode +/- 1, if fractional
    // scaling is used. If no scaling, then simply equivalent to output_mode.
    //
    // The surface's buffer should round these dimensions halfway away from zero
    // (e.g. math.h round()) to get the correct buffer size.
    //   See wp_viewport and wp_fractional_scale xmls for more information
    int32_t width_px_buffer;
    int32_t height_px_buffer;
    uint32_t fractional_scale_wp_120; // wp_fractional_scale

    struct zwlr_layer_surface_v1 *layer_surface;
    struct wp_fractional_scale_v1 *fractional_scale;
    struct wp_viewport *viewport;
};

struct scran_ui_textline_surface_state {
    BLPointI origin;
    int total_width_px;
};

struct scran_output_selectionSurface_buffer {
    struct scran_wl_buffer scran_wl_buffer;

    BLContextCore bl_ctx;
    BLImageCore bl_img;
    // TODO: Rename this here and elsewhere to e.g. selection_box_currently_drawn,
    // now that we have more things going on in the selection surface (like ui_keymap)?
    BLBoxI box_currently_drawn;

    struct scran_ui_textline_surface_state ui_greeting_state_currently_drawn;
    struct scran_ui_textline_surface_state ui_keymap_state_currently_drawn;
    struct scran_ui_textline_surface_state ui_statusline_state_currently_drawn;

    bool force_redraw;
    enum scran_ui_redrawn_textline_mask redrawn_textline_mask;
};

enum surface_theme {
    // HACK: Using this to make selection invisible
    //       TODO: Rework the surface redraw functions for more granular
    //       control over what to draw instead.
    SURFACE_THEME_PRE_SELECTION,

    SURFACE_THEME_DEFAULT,
    SURFACE_THEME_VIDEO_CAPTURE,
} SCRAN_PACKED;

enum scran_selection_surface_disable_reason {
    SCRAN_SELECTION_SURFACE_DISABLE_REASON_NONE                   = 0,
    SCRAN_SELECTION_SURFACE_DISABLE_REASON_IMAGE_HIDE       = 1 << 0,
    SCRAN_SELECTION_SURFACE_DISABLE_REASON_VIDEO_HIDE       = 1 << 0,
    SCRAN_SELECTION_SURFACE_DISABLE_REASON_FREEZEFRAME_HIDE = 1 << 0,
    SCRAN_SELECTION_SURFACE_DISABLE_REASON_FULLSCREEN_HIDE        = 1 << 1,
    SCRAN_SELECTION_SURFACE_DISABLE_REASON_UI_STAGE_FINISHED      = 1 << 2,
} SCRAN_PACKED;

struct scran_output_selectionSurface {
    struct scran_output_surface surface;
    struct scran_output_selectionSurface_buffer double_buffer[SELECTION_SURFACE_BUF_COUNT];

    struct scran_ui_context ui_ctx;

    BLPathCore bl_path;
    // XXX TODO: Turn this into a pointer once we remove the ugly redraw hack
    // in set_selection_surface_theme(). TODO: Redraw hack is gone now.
    BLBoxI box_last_drawn;
    struct scran_ui_textline_surface_state ui_greeting_state_last_drawn;
    struct scran_ui_textline_surface_state ui_keymap_state_last_drawn;
    struct scran_ui_textline_surface_state ui_statusline_state_last_drawn;

    // Disables frame callbacks and hiding/unhiding.
    enum scran_selection_surface_disable_reason disable_reason_mask;
    enum surface_theme                          theme;

    bool awaiting_frame_callback;
};

struct scran_output;
typedef void (*scran_output_callback)(struct scran_output *);

enum scran_capture_frame_consumers {
    SCRAN_CAPTURE_FRAME_CONSUMER_IMAGE       = 1 << 0,
    SCRAN_CAPTURE_FRAME_CONSUMER_VIDEO       = 1 << 1,
    SCRAN_CAPTURE_FRAME_CONSUMER_FREEZEFRAME = 1 << 2,
} SCRAN_PACKED;

struct capture_frame_context {
    struct ext_image_copy_capture_frame_v1 *frame;

    struct scran_output *output;
    struct scran_wl_buffer scran_wl_buffer;

    // set by pre-::ready event handlers
    enum wl_output_transform source_transform;
    // Contains *at least* the union of frame::damage-reported damage
    struct BLBoxI capture_buffer_damage_area_px;
    int64_t presentation_time_nsec;


    enum scran_capture_frame_consumers consumers;
};

struct capture_session_context {
    struct ext_image_copy_capture_session_v1 *wl_session;
    BLPointI source_dimensions_px;
    uint32_t shm_format;
    uint8_t pixel_stride;
};

struct capture_session {
    struct capture_frame_context frame_ctx;
    struct capture_session_context session_ctx;
};

enum scran_freezeframe_stage {
    SCRAN_FREEZEFRAME_STAGE_IDLE, // Does not imply hidden or showing
    SCRAN_FREEZEFRAME_STAGE_REFRESHING,
    SCRAN_FREEZEFRAME_STAGE_CAPTURING,
};

struct scran_output_freezeframe {
    struct scran_output_subsurface subsurface;

    struct capture_session session;

    enum scran_freezeframe_stage stage;
    bool showing;
    scran_output_callback callback;

    // Used when the captured frame needs to be transformed in memory before
    // being attached to the freezeframe surface.
    struct scran_wl_buffer surface_buffer;
};

struct scran_seat_pointerContext {
    BLPointI coordinates;

    // We only handle one button at a time
    // KEY_MAX is 2ff, but pointer::button sends uint32_t, so let's use that for now
    //     See: linux/input_event_codes.h.
    // NOTE: Should be set to SCRAN_BTN_NONE if force-switching selection state
    // or other state that depends on this, to prevent inversion of interpreted
    // button press/depress state.
    uint32_t active_button;
    // Only use press events; ignore release events.
    //     I.e. no button holding, and presses toggle the actions on/off
    //     (actions like rebasing, resizing, etc.).
    // XXX: This conceptually belongs in the scran_options struct, but keeping
    // it here for cache locality. If we start supporting multiple seats, then
    // probably just move it into scran_options.
    bool use_presses_only;

    uint32_t last_enter_serial;
    struct scran_output_selectionSurface *focused_selection_surface;

    // Safeguard to work around Hyprland #15899 stealing cursor focus
    // (not just keyboard focus) when mapping KEYBOARD_INTERACTION_EXCLUSIVE
    // layer-surfaces. This is a compositor bug, so probably just remove all
    // code referencing this some time after it's fixed upstream.
    bool pointer_focus_trusted;

    struct wp_cursor_shape_device_v1 *cursor_shape_device;
};

struct scran_seat_keyboard {
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    struct scran_output_selectionSurface *focused_selection_surface;

    bool last_press_was_untrusted_escape;
};

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

    struct scran_output_selectionSurface *active_selection_surface;
    bool mod_key_active;
};

enum selection_state {
    SELECTION_NONE,
    SELECTION_INITIALIZING,
    SELECTION_COMPLETE,
    SELECTION_REBASING,
    SELECTION_RESIZING,
} SCRAN_PACKED;

enum selection_resize_direction {
    SELECTION_RESIZE_NONE,
    SELECTION_RESIZE_TOP_LEFT,
    SELECTION_RESIZE_TOP_RIGHT,
    SELECTION_RESIZE_BOTTOM_LEFT,
    SELECTION_RESIZE_BOTTOM_RIGHT,
} SCRAN_PACKED;

// This struct is used as a shared context struct for event handlers that
// need to interact with the selection (capture area) state.
struct scran_output_selectionContext {
    // This is in selection-surface viewport-source-buffer coordinate-space.
    //   XXX NOTE: Most of the code at the moment assumes that this maps onto
    //   physical pixels +/- 1 pixel (still with the buffer's transform!!).
    //   This seems to hold true at the moment on Sway, COSMIC and Hyprland,
    //   once all layer_surface::configure events have fired.
    //   NOTE: Always deinverted. Update through selection_set_box_px().
    //   TODO: Make the coordinate space distinctions typed, in general?
    //           E.g. BLBoxISelection
    struct BLBoxI box_px;

    enum selection_state selection_state;
    bool size_is_frozen;

    // TODO: Clearer name? This should be used to store the pre-resize/rebase box
    struct BLBoxI box_before_changes_px;
    enum selection_resize_direction selection_resize_direction;
    int pointer_before_changes_x_px;
    int pointer_before_changes_y_px;
};

struct ffmpeg_context {
    // Video
    AVFormatContext *av_format_ctx;
    AVCodecContext  *av_codec_ctx;
    AVFrame         *av_frame_to_encode;
    AVPacket        *av_packet; // encoded frame

    // Audio
    AVCodecContext  *av_codec_ctx_audio;
    AVFrame         *av_frame_captured_audio;
    AVPacket        *av_packet_audio;
    AVAudioFifo     *av_audio_fifo;
};

enum scran_video_stage {
    SCRAN_VIDEO_STAGE_NONE,
    SCRAN_VIDEO_STAGE_FULLSCREEN_START_PENDING,
    SCRAN_VIDEO_STAGE_CAPTURING,
    SCRAN_VIDEO_STAGE_STOP_REQUESTED,
} SCRAN_PACKED;

struct scran_output_capture {
    struct ext_image_capture_source_v1 *source;

    struct capture_session session;
    struct ffmpeg_context ffmpeg_ctx;

    // Extra buffer for copying/intermediate operations
    // TODO: Rename this
    void *img_data_2;

    BLImageCore bl_img_captured;
    BLImageCodecCore bl_imgcodec;

    //  NOTE: selection_ctx_box_px should be updated synchronously with the
    //        drawn overlay's area (or be set based on the same real-time
    //        values). Otherwise, its graphics can spill into the capture frame.
    //        F.ex., the mouse can have moved in-between overlay's frame draw
    //        and capture's frame "draw".
    //        NOTE also that the most-recently drawn by us frame is *not*
    //        necessarily the correct frame to sync with. For some compositors,
    //        like Sway, this does result in proper sync, but some other
    //        compositors, like COSMIC, are not neatly ordered like this
    //        internally (at time of writing).
    struct BLBoxI selection_ctx_box_px;

    int64_t video_presentation_time_nsec_start;

    struct scran_stdout_reservation stdout_reservation;

    enum scran_capture_frame_consumers fullscreen_consumers;
    enum scran_capture_frame_consumers pending_fullscreen_consumers;

    enum surface_theme pre_capture_selection_theme;

    enum scran_video_stage video_stage;

    bool audio_active;
    bool audio_disable_modifier_active;
    // TODO: Merge this into the generic video start logic
    bool fullscreen_video_pending_audio_disabled;

    bool exit_after_capture;
};

struct scran_output_mode {
    // NOTE: These DO NOT have any transforms applied
    //       (but they are affected by resolution settings)
    int32_t width_px;
    int32_t height_px;
    int32_t refresh_rate_mHz;
};

// Global logical geometry
struct scran_output_xdg_geometry {
    // NOTE: These DO have transforms and scale already applied.
    int32_t x_logical;
    int32_t y_logical;
    int32_t w_logical;
    int32_t h_logical;
};

struct scran_output {
    struct wl_output *wl_output;

    struct scran_output_mode mode;
    // Logical geometry, as given by the xdg_output protocol:
    struct scran_output_xdg_geometry xdg_geometry;
    enum wl_output_transform transform;

    // NOTE: Must be initialized to 1, since this is our fallback if no
    // fractional scale is present
    int32_t scale;
    int32_t fractional_scale_cosmic_1000;   // zcosmic_output_head
    wl_fixed_t fractional_scale_wlr;        // zwlr_output_head

    struct scran_output_selectionSurface selection_surface;
    struct scran_cursor cursor;
    struct scran_output_selectionContext selection_ctx;
    struct scran_output_capture capture;
    struct scran_output_freezeframe freezeframe;

    BLBoxI initial_selection;

    // Only really needed during init and destruction:
    struct zxdg_output_v1 *xdg_output;

    char name[SCRAN_STATE_OUTPUT_NAME_SIZE]; // output::name
};

enum scran_opt_hide_ui_level {
    SCRAN_OPT_HIDE_UI_NONE,
    SCRAN_OPT_HIDE_UI_ITEMS,
    SCRAN_OPT_HIDE_UI_EVERYTHING,
} SCRAN_PACKED;

// TODO: Isolate this from scran state?
struct scran_options {
    char *output_path_filename_pointer; // TODO: Use offset instead
    char output_path[SCRAN_OUTPUT_FILEPATH_SIZE_MAX]; // NOTE: Also used as output_directory during cli arg init
    char filename_format[SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX];

    enum scran_opt_hide_ui_level hide_ui_level;
    bool output_to_stdout;
    bool no_keepalive;
    bool disable_audio_capture;
    bool disable_cursor_capture;
    bool freezeframe_at_startup;
    bool capture_and_exit_after_selection_init;
    bool produce_slurp;                 // output slurp-style geometry string
    bool no_notifications;
    bool have_custom_initial_selection;
    struct BLRectI custom_initial_selection_global_coordinates;
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
    atomic_int n_captures_in_progress;

    struct scran_options options;

    struct scran_globals globals;
    struct scran_seat seat;

    struct scran_stdout_reservation *active_stdout_reservation;

    // Used for releasing focus
    struct wl_region *empty_wl_region;
    struct scran_wl_buffer transparent_single_pixel_buffer;
    sig_atomic_t sig_focus_requested;
    bool focused;

    // TODO: Probably allocate this dynamically, after all.
    uint32_t n_outputs;
    struct scran_output outputs[MAX_OUTPUTS];
};


#endif
