#ifndef STATE_H
#define STATE_H

#include <stdbool.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "cursor-shape-v1.h"

// TODO: Buffer file/header?
#define SHM_FILENAME "/icicle-wayland-client"
#define BUF_COUNT 2
// ARGB8888 and XRGB8888 are always supported (wayland spec)
#define SURFACE_SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SURFACE_SHM_FORMAT_BL BL_FORMAT_PRGB32
#define SURFACE_BYTES_PER_PIXEL 4 // Depends on SURFACE_SHM_FORMAT  TODO: Get this from graphics library

struct client_state_globals {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct wl_output *output;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
    struct ext_output_image_capture_source_manager_v1 *output_image_capture_source_manager;
    struct ext_image_copy_capture_manager_v1 *image_copy_capture_manager;
};

struct client_state_surface_buffer {
    // TODO: Rename to wl_buffer to not mix it up with `data`?
    struct wl_buffer *buffer;
    void *data;
    bool busy;
    BLImageCore bl_img;
};

struct client_state_surface {
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    struct wl_shm_pool *shm_pool;
    uint32_t shm_pool_size; // TODO: Should this be int32_t ?
    struct client_state_surface_buffer double_buffer[BUF_COUNT];
    uint32_t buf_size;

    // TODO: Per-monitor/output
    bool is_focused;
    uint32_t width;
    uint32_t height;

    // TEST:
    uint32_t curr_color;
    uint32_t curr_width;
    uint32_t curr_height;
};

struct client_state_seat_pointer {
    struct wl_pointer *pointer;
    struct wp_cursor_shape_device_v1 *cursor_shape_device;

    enum wl_pointer_button_state btn_left_state;
    wl_fixed_t x;
    wl_fixed_t y;
};

struct client_state_seat_keyboard {
    struct wl_keyboard *keyboard;
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

struct client_state_selection_blend2d {
    BLContextCore ctx;
    BLPathCore path;

    struct BLBoxI box;
    // TODO: Maybe move this somewhere together with rebase_origin_pointer ?
    struct BLBoxI box_before_rebase;
    struct BLBoxI box_before_resize;
    struct BLBoxI box_outer;
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

struct client_state_selection {
    // bool selection_started;
    enum selection_state selection_state;
    enum selection_resize_direction selection_resize_direction;

    // TODO: Make a cleaner/more obvious interface for getting selection
    //       height/width etc. than just getting the .bl.box coordinates?
    struct client_state_selection_blend2d bl;

    wl_fixed_t rebase_origin_pointer_x;
    wl_fixed_t rebase_origin_pointer_y;
    wl_fixed_t resize_origin_pointer_x;
    wl_fixed_t resize_origin_pointer_y;

    // TODO: Not needed? Just use box only?
    // BLPoint bl_point_top_left;
    // BLPoint bl_point_bottom_right;
};

// TODO: Merge all or parts of this with client_state_surface_buffer?
struct client_state_capture_buffer {
    struct wl_buffer *buffer;
    void *data;
};

// TODO: Merge all or parts of this with client_state_surface?
struct client_state_capture {
    // Inconsistent naming, but my eyes are bleeding
    struct ext_image_capture_source_v1 *source;
    struct ext_image_copy_capture_session_v1 *session;
    // TODO: This doesn't need to be a state instance (nor do a lot of the others)
    struct ext_image_copy_capture_frame_v1 *frame;

    // TODO: Probably put this into a separate struct. Mode?
    //       Something to separate it from both capture/output and from xdg output
    uint32_t source_width_px;
    uint32_t source_height_px;
    uint32_t shm_format;
    bool shm_format_is_selected; // XXX: Is there a nicer way?

    // TODO: Use this
    enum wl_output_transform transform;

    struct client_state_capture_buffer buffer;
    uint32_t buf_size;
    struct wl_shm_pool *shm_pool;
    uint32_t shm_pool_size; // TODO: Should this be int32_t ?
};

struct client_state_output_mode {
    int32_t width;
    int32_t height;
    int32_t refresh_rate_mhz;
};

struct client_state_output {
    int32_t x_global;
    int32_t y_global;
    enum wl_output_subpixel subpixel_layout;
    enum wl_output_transform transform;

    int32_t scale;

    struct client_state_output_mode mode;
};

struct client_state {
    struct client_state_globals globals;
    struct client_state_surface surface;
    struct client_state_seat seat;
    struct client_state_output output;

    struct client_state_selection selection;
    struct client_state_capture capture;
};

#endif
