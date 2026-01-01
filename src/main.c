#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "wlr-layer-shell-unstable-v1.h"
#include "wayland-client-protocol.h"

#include "state.h"
#include "wayland-event-handlers.h"

#define SOCKNAME "wayland-1"
#define SOCKPATH "/run/user/1000/" SOCKNAME

static void
reset_selection(struct client_state *state)
{
    // TODO !!
}

// TODO: Split this up into more atomic parts?
//           F.ex. the roundtrip doesn't actually need to happen in here,
//           but putting it outside the function entirely means you have to
//           assume it was wasn't called earlier (to not roundtip more than
//           necesssary).
static inline bool
init_wayland_globals(struct client_state *state)
{
    struct client_state_globals *globals = &state->globals;

    // TODO: #ifdef DEBUG for prints?
    //           eprintf header with noop if not DEBUG ?

    fprintf(stderr, "Connecting to wayland socket '%s'.\n", SOCKNAME);

    globals->display = wl_display_connect(SOCKNAME);
    if (globals->display == NULL) {
        fprintf(stderr, "Failed to connect to wayland socket.\n");
        return false;
    }

    globals->registry = wl_display_get_registry(globals->display);
    if (globals->registry == NULL) {
        fprintf(stderr, "Failed to get wayland registry.\n");
        return false;
    }

    // Rest of globals initialized by registry_listener
    if ( wl_registry_add_listener(globals->registry, &registry_listener, (void *)state)
         == -1
    ) {
        fprintf(stderr, "Failed to add registry listener.\n");
        return false;
    }



    // wl_display_roundtrip runs wl_display_sync internally, and polls wl_callback
    // until all done-events are received and handled handled.
    //   (spec doesn't actually dictate implementation through wl_display_sync etc.,
    //    but libwayland does it like this at the time of writing.)
    if (wl_display_roundtrip(globals->display) == -1) {
        fprintf(stderr, "Display roundtrip after adding registry listener failed.\n");
        return false;
    }

    // TEST:
    fprintf(stderr, "Display roundtripped.\n");

    // TODO: Validate globals?

    return true;
}

static void
destroy_wayland_globals(struct client_state *state)
{
    struct client_state_globals *globals = &state->globals;

    // TODO: Is a roundtrip necessary?

    wl_compositor_destroy(globals->compositor);
    wl_seat_destroy(globals->seat);
    wl_shm_destroy(globals->shm);
    zwlr_layer_shell_v1_destroy(globals->layer_shell);
    wp_cursor_shape_manager_v1_destroy(globals->cursor_shape_manager);
    ext_output_image_capture_source_manager_v1_destroy(globals->output_image_capture_source_manager);
    ext_image_copy_capture_manager_v1_destroy(globals->image_copy_capture_manager);

    // (Doesn't actually need to be last)
    wl_registry_destroy(globals->registry);
}

// Open shm file, get fd, unlink file, return fd.
// The underlying file survives unlinking.
static inline int
shm_open_anon(void)
{
    // TODO: Generate random filenames in case file already exists?
    int fd = shm_open(SHM_FILENAME, O_CREAT | O_RDWR | O_EXCL, 0600);

    if (fd >= 0) {
        shm_unlink(SHM_FILENAME);
    }

    return fd;
}

// TODO: Clean this up a bit.
//       Also decide where the attaches/commits and roundtrips are placed and how many total
static inline bool
init_surface_shm_buffers(
    // TODO: Either switch this back to just state, or do this narrowing everywhere
    struct client_state_surface *st_surface,
    struct wl_shm *wl_shm_global
) {
    // TODO: Is this more efficient to create in handle_global and/or layer_surface ack_configure?
    // TODO: Close this.
    int shm_fd = shm_open_anon();
    // TODO: Graphics library needs to take part in this..
    st_surface->buf_size = BUF_PIXEL_BYTES * st_surface->width * st_surface->height;
    st_surface->shm_pool_size = BUF_COUNT * st_surface->buf_size;

    if (-1 == ftruncate(shm_fd, st_surface->shm_pool_size)) {
        fprintf(stderr, "Failed to resize shm file to %d\n", st_surface->shm_pool_size);
        close(shm_fd);
        return false;
    }

    fprintf(stderr, "Resized shm file to %d\n", st_surface->shm_pool_size);

    // TODO: Handle wl_shm::format event
    st_surface->shm_pool = wl_shm_create_pool(
        wl_shm_global,
        shm_fd,
        st_surface->shm_pool_size
    );

    for (int i = 0; i < BUF_COUNT; i++) {
        fprintf(stderr, "Creating buffer %d\n", i);
        assert(i * st_surface->buf_size <= st_surface->shm_pool_size);

        int _pool_offset = i * st_surface->buf_size;

        // TODO: Don't mmap per buffer...
        st_surface->double_buffer[i].data = mmap(
            NULL, st_surface->shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, _pool_offset
        );

        st_surface->double_buffer[i].buffer = wl_shm_pool_create_buffer(
            st_surface->shm_pool,
            _pool_offset,
            st_surface->width,
            st_surface->height,
            BUF_PIXEL_BYTES * st_surface->width,
            BUF_FORMAT
        );

        wl_buffer_add_listener(
            st_surface->double_buffer[i].buffer,
            &buffer_listener,
            &st_surface->double_buffer[i]
        );

    }

    close(shm_fd);
    wl_shm_pool_destroy(st_surface->shm_pool);

    // TODO: Should this be done here?
    wl_surface_attach(st_surface->surface, st_surface->double_buffer[0].buffer, 0, 0);

    return true;
}

static bool
destroy_surface_shm_buffers(struct client_state_surface *st_surface)
{
    for (int i = 0; i < BUF_COUNT; ++i) {
        wl_buffer_destroy(st_surface->double_buffer[i].buffer);
    }

    return true;
}

static inline bool
init_surface(struct client_state *state)
{
    // Must add role to surface and ack its configure event before adding a buffer.
    state->surface.surface = wl_compositor_create_surface(state->globals.compositor);
    state->surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state->globals.layer_shell,
        state->surface.surface,
        NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "test_client_namespace" // TODO: Figure out a namespace name?
    );

    zwlr_layer_surface_v1_set_exclusive_zone(state->surface.layer_surface, -1);
    // Need to set at least anchors before configure event,
    // so that the compositor knows what width/height to give us.
    zwlr_layer_surface_v1_set_anchor(
        state->surface.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
    );

    zwlr_layer_surface_v1_add_listener(state->surface.layer_surface, &layer_surface_listener, state);
    // Initial bufferless commit to trigger configure event
    wl_surface_commit(state->surface.surface);

    // XXX: This also triggers initial wl_seat listener events at the moment,
    //      due to nested add_listener functions
    //          seat_listener --> pointer_listener, keyboard_listener ...
    if (-1 == wl_display_roundtrip(state->globals.display)) {
        fprintf(stderr, "Display roundtrip after adding zwlr_layer_surface_v1 listener failed.\n");
        return false;
    }

    return true;
}

static bool
init_seat(struct client_state *state)
{
    // wl_seat_

    return true;
}

static inline bool
init_selection_and_blend2d(struct client_state *state)
{
    struct client_state_selection *selection = &state->selection;
    struct client_state_selection_blend2d *bl = &selection->bl;

    bl_context_init(&bl->ctx);
    bl_path_init(&bl->path);

    // XXX: Maybe handle this assert more robustly
    assert(state->surface.width != 0);
    bl->box_outer = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = state->surface.width,
        .y1 = state->surface.height,
    };

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    for (int i = 0; i < BUF_COUNT; ++i) {
        struct client_state_surface_buffer *st_buffer = &state->surface.double_buffer[i];
        // Shared memory must already be allocated.
        assert(st_buffer->data != NULL);

        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            state->surface.width,
            state->surface.height,
            BUF_FORMAT_BL,
            st_buffer->data,
            BUF_PIXEL_BYTES * state->surface.width,
            BL_DATA_ACCESS_RW,
            NULL, // TODO: - Let blend2d destroy our data?
            NULL  //       - Ditto
        );
    }

    return true;
}

int main(void)
{
    // TODO: memset? or explicit zeroing where required?
    // struct client_state state = { 0 };
    struct client_state state;
    memset(&state, 0, sizeof(struct client_state));

    // First roundtrip:
    if (!init_wayland_globals(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_wayland_globals()\n");

    // Second roundtrip:
    if (!init_surface(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_surface()\n");

    if (!init_surface_shm_buffers(&state.surface, state.globals.shm)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_surface_shm_buffers()\n");

    if (!init_seat(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_seat()\n");

    if (!init_selection_and_blend2d(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_selection()\n");


    // Initial frame callback request.
    // All subsequent requests are done "recursively" from within the listener's
    // 'done' event handler
    wl_callback_add_listener(
        wl_surface_frame(state.surface.surface),
        &surface_frame_callback_listener,
        &state
    );
    wl_surface_commit(state.surface.surface);

    while (wl_display_dispatch(state.globals.display)) {
        // TODO: state->running
        //       Exit with keybind
    }


    // TODO: Remember to fix off-by-one bug when selecting corner to corner
    //           F.ex. 2559x1599 rect width/height

    // todo: destroy wl_proxy and wl_event_queue objects when created
    destroy_surface_shm_buffers(&state.surface);
    destroy_wayland_globals(&state);

    wl_display_disconnect(state.globals.display);
    fprintf(stderr, "Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

