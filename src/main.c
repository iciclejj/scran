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

// TODO: Decide on inline

// TODO: Split this up into more atomic parts?
//           F.ex. the roundtrip doesn't actually need to happen in here,
//           but putting it outside the function entirely means you have to
//           assume it was wasn't called earlier (to not roundtip more than
//           necesssary).
static inline bool
init_wayland_globals_and_roundtrip(struct client_state *state)
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
    // XXX: MEMORY ALLOC/FREE HERE
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
init_selection_and_blend2d(struct client_state_output *st_output)
{
    struct client_state_output_selection_blend2d *bl = &st_output->selection.bl;
    struct client_state_output_surface * st_surface = &st_output->surface;

    bl_context_init(&bl->ctx);
    bl_path_init(&bl->path);

    // XXX: Maybe handle this assert more robustly
    assert(st_surface->width_px != 0);
    bl->box_outer = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = st_surface->width_px,
        .y1 = st_surface->height_px,
    };

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct client_state_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];
        // Shared memory must already be allocated.
        assert(st_buffer->data != NULL);

        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            st_surface->width_px,
            st_surface->height_px,
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_PIXEL_STRIDE * st_surface->width_px,
            BL_DATA_ACCESS_RW,
            NULL,
            NULL
        );
    }

    return true;
}

static inline bool
init_image_capture_source(
    struct client_state_output *st_output,
    struct client_state_globals *globals
) {
    st_output->capture.source = ext_output_image_capture_source_manager_v1_create_source(
        globals->output_image_capture_source_manager,
        st_output->wl_output
    );

    return true;
}

static inline bool
init_image_copy_capture_session(
    struct client_state_output *st_output,
    struct client_state_globals *globals
) {
    st_output->capture.session = ext_image_copy_capture_manager_v1_create_session(
        globals->image_copy_capture_manager,
        st_output->capture.source,
        // TODO: Make this optional
        EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS
    );
    ext_image_copy_capture_session_v1_add_listener(
        st_output->capture.session,
        &image_copy_capture_session_listener,
        st_output
    );

    return true;
}

static inline bool
init_image_copy_capture_shm_buffer(
    struct client_state_output *st_output,
    struct client_state_globals *globals
) {
    // TODO: Somehow assert session::shm_buffer has ran?
    if (!st_output->capture.shm_format_is_selected) {
        fprintf(stderr, "Failed to select shm_buffer format.\n");
        return false;
    }

    // Full output source buffer for now.
    // TODO: Revisit this after multi-output support.
    st_output->capture.buf_size =
        st_output->capture.source_width_px
        * st_output->capture.pixel_stride
        * st_output->capture.source_height_px;
    st_output->capture.shm_pool_size = st_output->capture.buf_size;

    int shm_fd = shm_open_anon();

    // XXX: MEMORY ALLOC/FREE HERE
    if (-1 == ftruncate(shm_fd, st_output->capture.shm_pool_size)) {
        fprintf(stderr, "Failed to resize shm file to %d\n", st_output->capture.shm_pool_size);
        close(shm_fd);
        return false;
    }

    // TODO: Use same pool as surface?
    st_output->capture.shm_pool = wl_shm_create_pool(
        globals->shm,
        shm_fd,
        st_output->capture.shm_pool_size
    );

    st_output->capture.buffer.buffer = wl_shm_pool_create_buffer(
        st_output->capture.shm_pool,
        0,
        st_output->capture.source_width_px,
        st_output->capture.source_height_px,
        st_output->capture.pixel_stride * st_output->capture.source_width_px,
        st_output->capture.shm_format
    );

    // XXX: MEMORY ALLOC/FREE HERE
    st_output->capture.buffer.data = mmap(
        0, st_output->capture.buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0
    );

    // XXX: MEMORY ALLOC/FREE HERE
    close(shm_fd);
    wl_shm_pool_destroy(st_output->capture.shm_pool);

    if (st_output->capture.buffer.data == NULL) {
        return false;
    }

    return true;
}

static inline void
destroy_capture_shm_buffers(struct client_state_output_capture *st_capture)
{
    // XXX: MEMORY ALLOC/FREE HERE
    wl_buffer_destroy(st_capture->buffer.buffer);
}

int main(void)
{
    struct client_state state = { };

    // TODO: Systematize and minimize roundtrips/syncs
    //       Handle errors/return false where appropriate
    //           Probably void function if false return never happens
    //       Probably refactor init_* function atomicity after code is more settled
    //       Allow selection before capture protocols are ready?
    //           Probably negligible and difficult without multithreading

    // First roundtrip:
    if (!init_wayland_globals_and_roundtrip(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_wayland_globals()\n");


    // indexing into .buffer.data, i.e. the screen/output buffer that encapsulates
    // the selection/capture area
    // [34560] => 16 UHD monitors stacked vertically ~= 0.5 MB (x86_64)
    // XXX: Temporarily placed here while collecting memory allocations.
    const uint32_t frame_iovec_len = 34560;
    struct iovec *frame_iovec_memory = malloc(
        sizeof(struct iovec) * state.n_outputs * frame_iovec_len
    );
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        _st_output->capture.frame_iovec_size = frame_iovec_len;
        _st_output->capture.frame_iovec = frame_iovec_memory + i * frame_iovec_len;
    }

    assert(state.n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        // TODO: Check memory management
        if (!init_output_surface(_st_output, &state.globals)) {
            return false;
        }
        if (!init_output_surface_shm_buffers(&_st_output->surface, state.globals.shm)) {
            return false;
        }
        if (!init_selection_and_blend2d(_st_output)) {
            return EXIT_FAILURE;
        }

        if (!init_image_capture_source(_st_output, &state.globals)) {
            return EXIT_FAILURE;
        }
        if (!init_image_copy_capture_session(_st_output, &state.globals)) {
            return EXIT_FAILURE;
        }
        // TODO: Figure out where to roundtrip
        wl_display_roundtrip(state.globals.display);
        if (!init_image_copy_capture_shm_buffer(_st_output, &state.globals)) {
            return EXIT_FAILURE;
        }

        // Initial frame callback request.
        // All subsequent requests are done "recursively" from within the listener's
        // 'done' event handler
        wl_callback_add_listener(
            wl_surface_frame(_st_output->surface.surface),
            &surface_frame_callback_listener,
            _st_output
        );
        wl_surface_commit(_st_output->surface.surface);
    }

    while ( // Main event loop...
        !state.exit_requested
        && wl_display_dispatch(state.globals.display)
    );

    // TODO: Remember to fix off-by-one bug when selecting corner to corner
    //           F.ex. 2559x1599 rect width/height

    assert(state.n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        // todo: destroy wl_proxy and wl_event_queue objects when created
        // XXX: MEMORY ALLOC/FREE HERE
        destroy_output_surface_shm_buffers(&_st_output->surface);
        destroy_capture_shm_buffers(&_st_output->capture);
        destroy_wayland_globals(&state);
    }

    wl_display_disconnect(state.globals.display);
    fprintf(stderr, "Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

