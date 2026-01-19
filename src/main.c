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
#include "event-handlers.h"
#include "init.h"
#include "print.h"

// TODO:
//     Move init/ code back in here or put init code in there consistently...
//     Don't use libwayland..? Handle allocations etc. ourselves?
//

// TODO: Dynamically find this name
#define SOCKNAME "wayland-1"
#define SOCKPATH "/run/user/1000/" SOCKNAME

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


// TODO: void return type?
static bool
init_premem(struct client_state *state)
{
    state->globals.display = wl_display_connect(SOCKNAME);

    if (state->globals.display == NULL) {
        eprintf("Failed to connect to wayland socket.\n");
        return false;
    } else {
        eprintf("Connected to wayland socket.\n");
    }

    state->globals.registry = wl_display_get_registry(state->globals.display);

    // Remaining globals get bound by registry_listener::global during the first roundtrip
    // TODO: Validate globals (at least non-stable protocols)
    wl_registry_add_listener(state->globals.registry, &registry_listener, (void *)state);
    wl_display_roundtrip(state->globals.display);
    DEBUG("Roundtripped after adding registry listener()\n");


    //   Collect dynamic memory requirements
    // + Initialize otherwhat lacking extra dependencies (beyond globals)
    for (int i = 0; i < state->n_outputs; ++i) {
        struct client_state_output *_st_output = &state->outputs[i];

        if (!init_output_surface(_st_output, &state->globals)) {
            return false;
        }

        if (!init_capture(_st_output, &state->globals)) {
            return false;
        }
    }
    // Then roundtrip to collect listener-provided memory requirements into our state struct.
    // This should be our last required roundtrip until the main event loop dispatch.
    wl_display_roundtrip(state->globals.display);

    return true;
}

// TODO:
//  - Add --slim/--no-video arg that skips allocating video-only requirements,,
//    extra frame buffers, etc.
static bool
init_meminit(
    struct client_state *state,
    void **shm_addr,
    size_t *shm_size_bytes
) {
    // Calculate memory requirements
    for (int i = 0; i < state->n_outputs; ++i) {
        struct client_state_output *_st_output = &state->outputs[i];

        // XXX: Handle this gracefully (and maybe in a nicer location?)
        if (_st_output->capture.shm_format == -1) {
            DEBUG("Failed to select shm_buffer format.\n");
            return false;
        }

        const ssize_t _surface_buf_bytes = SURFACE_BUF_COUNT * GET_SURFACE_BUF_SIZE(_st_output->mode);
        const ssize_t _capture_buf_bytes = GET_CAPTURE_BUF_SIZE((*_st_output));
        // TODO: persistent libav allocations
        // selection: No manual allocations

        *shm_size_bytes += _surface_buf_bytes
                                + _capture_buf_bytes;
    }

    int global_pool_shm_fd = shm_open_anon();
    if (ftruncate(global_pool_shm_fd, *shm_size_bytes) == -1) {
        DEBUG("Failed to resize shm file to %zu\n", *shm_size_bytes);
        close(global_pool_shm_fd);
        return false;
    }

    *shm_addr = mmap(NULL, *shm_size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, global_pool_shm_fd, 0);
    // TODO: Only allocate what the server will actually need.
    //           F.ex., the server doesn't need to have libav objects.
    struct wl_shm_pool *global_pool_wl = wl_shm_create_pool(
        state->globals.shm,
        global_pool_shm_fd,
        *shm_size_bytes
    );

    // Assign allocated memory
    ssize_t curr_offset = 0;
    for (int i = 0; i < state->n_outputs; ++i) {
        struct client_state_output *_st_output = &state->outputs[i];

        assert(SURFACE_BUF_COUNT == A_DOUBLE_BUFFER_HAS_TWO_BUFFERS && SURFACE_BUF_COUNT == 2);

        for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
            _st_output->surface.double_buffer[i].data = *shm_addr + curr_offset;
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
                &surface_buffer_listener,
                &_st_output->surface.double_buffer[i]
            );

            // XXX TODO: Should this be done here?
            wl_surface_attach(_st_output->surface.surface, _st_output->surface.double_buffer[0].buffer, 0, 0);
        }

        _st_output->capture.frame_ctx.st_buffer.data = *shm_addr + curr_offset;
        _st_output->capture.frame_ctx.st_buffer.buffer = wl_shm_pool_create_buffer(
            global_pool_wl,
            curr_offset,
            _st_output->mode.width_px,
            _st_output->mode.height_px,
            GET_CAPTURE_STRIDE((*_st_output)),
            _st_output->capture.shm_format
        );
        curr_offset += GET_CAPTURE_BUF_SIZE((*_st_output));

        wl_buffer_add_listener(
            _st_output->capture.frame_ctx.st_buffer.buffer,
            &capture_buffer_listener,
            &_st_output->capture.frame_ctx.st_buffer
        );
    }

    assert(curr_offset == *shm_size_bytes);

    // (wayland's mmaps and fd references live on)
    wl_shm_pool_destroy(global_pool_wl);
    close(global_pool_shm_fd);

    return true;
}

static bool
init_postmem(struct client_state *state)
{
    assert(state->n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < state->n_outputs; ++i) {
        struct client_state_output *_st_output = &state->outputs[i];

        if (!init_selection_and_blend2d(_st_output)) {
            return false;
        }

        // Initial frame callback request.
        // All subsequent requests are done "recursively" from within ::done
        wl_callback_add_listener(
            wl_surface_frame(_st_output->surface.surface),
            &surface_frame_callback_listener,
            _st_output
        );

        // NOTE: Most "init" time is spent here (during first dispatch),
        //       waiting to enter layer_surface::configure (Ex: ~7000us)
        //          XXX: This part seems possibly framerate-bound/vsynced ?
        //       And remaining time is mostly during the first roundtrip to get
        //       globals. (Ex: ~2000us)
        //         - Second roundtrip where we collect memory requirements is
        //           relatively fast (Ex: ~130us)
        wl_surface_commit(_st_output->surface.surface);
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
    void *shm_addr = NULL;
    size_t shm_size_bytes = 0;

    if (!init_premem(&state)) {
        eprintf("Failed pre-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }

    if (!init_meminit(&state, &shm_addr, &shm_size_bytes)) {
        eprintf("Failed to initialize memory and/or shared memory buffers.\n");
        return EXIT_FAILURE;
    }

    if (!init_postmem(&state)) {
        eprintf("Failed post-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }


    // Main event loop.
    //
    // This will also finalize any initialization that has not roundtripped yet.
    //
    // TODO:
    //   - Find out if we can bypass (sway/wlroot's?) seemingly framerate-bound
    //     startup latency for reaching layer_surface::configure. Or potentially
    //     fix it, if it is a bug.
    //     The initializing ::commit shouldn't need to be vsynced..?
    while (
        !state.exit_requested
        &&
        -1 != wl_display_dispatch(state.globals.display)
    );



    assert(state.n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < state.n_outputs; ++i) {
        struct client_state_output *_st_output = &state.outputs[i];

        // TODO: Destroy wayland objects
    }

    munmap(shm_addr, shm_size_bytes);
    destroy_wayland_globals(&state);

    wl_display_disconnect(state.globals.display);
    eprintf("Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

