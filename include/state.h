#ifndef STATE_H
#define STATE_H

#include <stdbool.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "cursor-shape-v1.h"

// TODO: Buffer file/header?
#define SHM_FILENAME "/icicle-wayland-client"
#define BUF_COUNT 2
// TODO: Get supported formats - compositor::format
#define BUF_FORMAT WL_SHM_FORMAT_ARGB8888
#define BUF_FORMAT_BL BL_FORMAT_PRGB32
#define BUF_PIXEL_BYTES 4 // Depends on BUF_FORMAT  TODO: Get this from graphics library

struct client_state_globals {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
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
    uint32_t shm_pool_size;
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
    struct BLRectI rect;
};

enum selection_state {
    SELECTION_NONE,
    SELECTION_IN_PROGRESS,
    SELECTION_COMPLETE,
};

struct client_state_selection {
    // bool selection_started;
    enum selection_state selection_state;

    struct client_state_selection_blend2d bl;

    // TODO: Not needed? Just use box only?
    // BLPoint bl_point_top_left;
    // BLPoint bl_point_bottom_right;
};

struct client_state {
    struct client_state_globals globals;
    struct client_state_surface surface;
    struct client_state_seat seat;

    struct client_state_selection selection;
};

#endif
