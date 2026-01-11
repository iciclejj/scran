#include <stdio.h>
#include <stdlib.h>
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

// TODO: Dynamically find this name
#define SOCKNAME "wayland-1"
#define SOCKPATH "/run/user/1000/" SOCKNAME

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

    fprintf(stderr, "Finished: init_wayland_globals()\n");

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
    // TODO: Add remaining...

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
    assert(st_output->mode.width_px != 0);
    bl->box_outer = (struct BLBoxI) {
        .x0 = 0,
        .y0 = 0,
        .x1 = st_output->mode.width_px,
        .y1 = st_output->mode.height_px,
    };

    // TODO: Should maybe be a separate function, f.ex. init_surface_buffers_blend2d
    //       and called directly from main, after init_surface_shm_buffers
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        struct client_state_output_surface_buffer *st_buffer = &st_surface->double_buffer[i];
        // Shared memory must already be allocated.
        assert(st_buffer->data != NULL);

        bl_image_init_as_from_data(
            &st_buffer->bl_img,
            st_output->mode.width_px,
            st_output->mode.height_px,
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_PIXEL_STRIDE * st_output->mode.width_px,
            BL_DATA_ACCESS_RW,
            NULL,
            NULL
        );
    }

    return true;
}

// TODO: Allow selection before capture protocols are ready?
//           Probably negligible and difficult without multithreading
//       Probably find a cleaner way to do this multi-step init?
//       Remember to fix off-by-one bug when selecting corner to corner
//           F.ex. 2559x1599 rect width/height
int main(void)
{
    struct client_state state = { };

    if (!init_wayland_globals_and_roundtrip(&state)) {
        return EXIT_FAILURE;
    }

    //   Collect dynamic memory requirements
    // + Initialize otherwhat lacking extra dependencies (beyond globals)
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        if (!init_output_surface(_st_output, &state.globals)) {
            return EXIT_FAILURE;
        }

        if (!init_capture(_st_output, &state.globals)) {
            return EXIT_FAILURE;
        }
    }
    // Then roundtrip to collect listener-provided memory requirements into state
    // This should be our last required roundtrip until main event loop's dispatch
    wl_display_roundtrip(state.globals.display);

    // Calculate memory requirements
    int global_pool_size_bytes = 0;
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        // XXX: Handle this gracefully (and maybe in a nicer location?)
        if (_st_output->capture.shm_format == -1) {
            fprintf(stderr, "Failed to select shm_buffer format.\n");
            return EXIT_FAILURE;
        }

        const ssize_t _surface_buf_bytes = SURFACE_BUF_COUNT * GET_SURFACE_BUF_SIZE(_st_output->mode);
        const ssize_t _capture_buf_bytes = GET_CAPTURE_BUF_SIZE((*_st_output));
        const ssize_t _capture_buf_iov_bytes = GET_CAPTURE_IOV_SIZE((*_st_output));
        // selection: No manual allocations

        global_pool_size_bytes += _surface_buf_bytes
                                + _capture_buf_bytes
                                + _capture_buf_iov_bytes;
    }

    int global_pool_shm_fd = shm_open_anon();
    if (ftruncate(global_pool_shm_fd, global_pool_size_bytes) == -1) {
        fprintf(stderr, "Failed to resize shm file to %d\n", global_pool_size_bytes);
        close(global_pool_shm_fd);
        return EXIT_FAILURE;
    }

    void *global_pool = mmap(NULL, global_pool_size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, global_pool_shm_fd, 0);
    struct wl_shm_pool *global_pool_wl = wl_shm_create_pool(
        state.globals.shm,
        global_pool_shm_fd,
        global_pool_size_bytes
    );

    // Assign allocated memory
    ssize_t curr_offset = 0;
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        assert(SURFACE_BUF_COUNT == A_DOUBLE_BUFFER_HAS_TWO_BUFFERS && SURFACE_BUF_COUNT == 2);

        for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
            _st_output->surface.double_buffer[i].data = global_pool + curr_offset;
            _st_output->surface.double_buffer[i].buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                curr_offset,
                _st_output->mode.width_px,
                _st_output->mode.height_px,
                GET_SURFACE_STRIDE(_st_output->mode),
                SURFACE_SHM_FORMAT
            );
            curr_offset += GET_SURFACE_BUF_SIZE(_st_output->mode);

            wl_buffer_add_listener(
                _st_output->surface.double_buffer[i].buffer,
                &buffer_listener,
                &_st_output->surface.double_buffer[i]
            );

            // XXX TODO: Should this be done here?
            wl_surface_attach(_st_output->surface.surface, _st_output->surface.double_buffer[0].buffer, 0, 0);
        }

        _st_output->capture.buffer.data = global_pool + curr_offset;
        _st_output->capture.buffer.buffer = wl_shm_pool_create_buffer(
            global_pool_wl,
            curr_offset,
            _st_output->mode.width_px,
            _st_output->mode.height_px,
            GET_CAPTURE_STRIDE((*_st_output)),
            _st_output->capture.shm_format
        );
        curr_offset += GET_CAPTURE_IOV_SIZE((*_st_output));

        wl_buffer_add_listener(
            _st_output->capture.buffer.buffer,
            &buffer_listener,
            &_st_output->capture.buffer
        );

        _st_output->capture.frame_iovec =  global_pool + curr_offset;
    }

    // (wayland's mmaps and fd references live on)
    wl_shm_pool_destroy(global_pool_wl);
    close(global_pool_shm_fd);

    // Memory init finished - run remaining dependent initialization
    assert(state.n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        if (!init_selection_and_blend2d(_st_output)) {
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

    assert(state.n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        // TODO: Destroy wayland objects
    }
    munmap(global_pool, global_pool_size_bytes);
    destroy_wayland_globals(&state);

    wl_display_disconnect(state.globals.display);
    fprintf(stderr, "Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

