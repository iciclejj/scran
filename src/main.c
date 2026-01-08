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

#include "ext-image-copy-capture-v1.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "wayland-client-protocol.h"

#include "state.h"
#include "wayland-event-handlers.h"
#include "init.h"

#define SOCKNAME "wayland-1"
#define SOCKPATH "/run/user/1000/" SOCKNAME

static void
reset_selection(struct client_state *state)
{
    // TODO !!
}

// TODO: Decide on inline

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

static inline bool
init_seat(struct client_state *state)
{
    // All init happens in seat_listener for now...

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
    assert(state->surface.width_px != 0);
    bl->box_outer = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = state->surface.width_px,
        .y1 = state->surface.height_px,
    };

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct client_state_surface_buffer *st_buffer = &state->surface.double_buffer[i];
        // Shared memory must already be allocated.
        assert(st_buffer->data != NULL);

        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            state->surface.width_px,
            state->surface.height_px,
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_PIXEL_STRIDE * state->surface.width_px,
            BL_DATA_ACCESS_RW,
            NULL, // TODO: - Let blend2d destroy our data?
            NULL  //       - Ditto
        );
    }

    return true;
}

static inline bool
init_output(struct client_state *state)
{
    // All init happens in output_listener for now...

    return true;
}

static inline bool
init_image_capture_source(struct client_state *state)
{
    state->capture.source = ext_output_image_capture_source_manager_v1_create_source(
        state->globals.output_image_capture_source_manager,
        state->globals.output
    );

    return true;
}

static inline bool
init_image_copy_capture_session(struct client_state *state)
{
    state->capture.session = ext_image_copy_capture_manager_v1_create_session(
        state->globals.image_copy_capture_manager,
        state->capture.source,
        // TODO: Make this optional
        EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
    );
    ext_image_copy_capture_session_v1_add_listener(
        state->capture.session,
        &image_copy_capture_session_listener,
        state
    );

    return true;
}

static inline bool
init_image_copy_capture_shm_buffer(struct client_state *state)
{
    if (!state->capture.shm_format_is_selected) {
        fprintf(stderr, "Failed to select shm_buffer format.\n");
        return false;
    }

    // Full output source buffer for now.
    // TODO: Revisit this after multi-output support.
    state->capture.buf_size =
        state->capture.source_width_px
        * state->capture.pixel_stride
        * state->capture.source_height_px;
    state->capture.shm_pool_size = state->capture.buf_size;

    int shm_fd = shm_open_anon();

    if (-1 == ftruncate(shm_fd, state->capture.shm_pool_size)) {
        fprintf(stderr, "Failed to resize shm file to %d\n", state->capture.shm_pool_size);
        close(shm_fd);
        return false;
    }

    // TODO: Use same pool as surface?
    state->capture.shm_pool = wl_shm_create_pool(
        state->globals.shm,
        shm_fd,
        state->capture.shm_pool_size
    );

    state->capture.buffer.buffer = wl_shm_pool_create_buffer(
        state->capture.shm_pool,
        0,
        state->surface.width_px,
        state->surface.height_px,
        state->capture.pixel_stride * state->capture.source_width_px,
        state->capture.shm_format
    );

    state->capture.buffer.data = mmap(
        0, state->capture.buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0
    );

    close(shm_fd);
    wl_shm_pool_destroy(state->capture.shm_pool);

    if (state->capture.buffer.data == NULL) {
        return false;
    }

    return true;
}

static inline void
destroy_capture_shm_buffers(struct client_state_capture *st_capture)
{
    wl_buffer_destroy(st_capture->buffer.buffer);
}

int main(void)
{
    // TODO: memset? or explicit zeroing where required?
    // struct client_state state = { 0 };
    struct client_state state;
    memset(&state, 0, sizeof(struct client_state));

    // TODO: Systematize and minimize roundtrips/syncs
    //       Handle errors/return false where appropriate
    //           Probably void function if false return never happens
    //       Probably refactor init_* function atomicity after code is more settled
    //       Allow selection before capture protocols are ready?
    //           Probably negligible and difficult without multithreading

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

    if (!init_output(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_output()\n");

    // TODO: Will need xdg_output for logical geometry

    if (!init_image_capture_source(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_image_capture_source()\n");

    if (!init_image_copy_capture_session(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_image_copy_capture_session()\n");

    // TODO: Figure out where to roundtrip
    wl_display_roundtrip(state.globals.display);

    if (!init_image_copy_capture_shm_buffer(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_image_copy_capture_shm_buffer()\n");

    // Initial frame callback request.
    // All subsequent requests are done "recursively" from within the listener's
    // 'done' event handler
    wl_callback_add_listener(
        wl_surface_frame(state.surface.surface),
        &surface_frame_callback_listener,
        &state
    );
    wl_surface_commit(state.surface.surface);

    while ( // Main event loop...
        !state.exit_requested
        && wl_display_dispatch(state.globals.display)
    );

    // TODO: Remember to fix off-by-one bug when selecting corner to corner
    //           F.ex. 2559x1599 rect width/height

    // todo: destroy wl_proxy and wl_event_queue objects when created
    destroy_surface_shm_buffers(&state.surface);
    destroy_capture_shm_buffers(&state.capture);
    destroy_wayland_globals(&state);

    wl_display_disconnect(state.globals.display);
    fprintf(stderr, "Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

