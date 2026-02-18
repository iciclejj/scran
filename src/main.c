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

#include "wayland-client-protocol.h"

#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "init.h"
#include "print.h"

// XXX TODO: Rename this to g_scran or g_scran_state, probably.
struct scran g_state = { };

// TODO:
//     Move init/ code back in here or put init code in there consistently...
//     Don't use libwayland..? Handle allocations etc. ourselves?
//

// TODO: Dynamically find this name
#define SOCKNAME "wayland-1"
#define SOCKPATH "/run/user/1000/" SOCKNAME


static bool
init_premem()
{
    g_state.globals.display = wl_display_connect(SOCKNAME);

    if (g_state.globals.display == NULL) {
        eprintf("Failed to connect to wayland socket.\n");
        return false;
    } else {
        eprintf("Connected to wayland socket.\n");
    }

    g_state.globals.registry = wl_display_get_registry(g_state.globals.display);

    // Remaining globals get bound by registry_listener::global during the first roundtrip
    // TODO: Validate globals (at least non-stable protocols)
    wl_registry_add_listener(g_state.globals.registry, &registry_listener, &g_state);
    wl_display_roundtrip(g_state.globals.display);
    DEBUG("Roundtripped after adding registry listener()\n");

    if (g_state.n_outputs < 1) {
        eprintf("No outputs detected.\n");
        return 0;
    }

    //   Collect dynamic memory requirements
    // + Initialize otherwhat lacking extra dependencies (beyond globals)
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        if (!init_premem__surface(_st_output, &g_state.globals)) {
            return false;
        }

        if (!init_premem__capture(_st_output, &g_state.seat.datacontrol, &g_state.globals)) {
            return false;
        }
    }
    // Then roundtrip to collect listener-provided memory requirements into our state struct.
    // This should be our last required roundtrip until the main event loop dispatch.
    wl_display_roundtrip(g_state.globals.display);

    return true;
}

static inline void
_stay_alive_while_clipboard_active()
{
    bool *const clipboard_active = &g_state.seat.datacontrol.selection_active;

    if (*clipboard_active == true) {
        eprintf("Keeping clipboard selection alive until stolen...\n");

        while (*clipboard_active == true) {
            wl_display_dispatch(g_state.globals.display);
        }

        eprintf("Clipboard selection stolen! Continuing exit.\n");
    }
}

static void
init_premem__destroy()
{
    assert(g_state.n_outputs <= MAX_OUTPUTS);

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        init_premem__surface__destroy(_st_output);
        init_premem__capture__destroy(_st_output);
    }

    // TODO: Make sure this happens at an appropriate point in time (memory
    // footprint should be minimized), once the init/cleanup is more
    // finalized.
    _stay_alive_while_clipboard_active();

    registry_listener__destroy(&g_state);
}

// TODO:
//  - Add --slim/--no-video arg that skips allocating video-only requirements,,
//    extra frame buffers, etc.
static bool
init_meminit(
    void **shm_addr,
    size_t *shm_size_bytes
) {
    // Calculate memory requirements
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        // XXX: Handle this gracefully (and maybe in a nicer location?)
        if (_st_output->capture.shm_format == -1) {
            DEBUG("Failed to select shm_buffer format.\n");
            return false;
        }

        // TODO: Alignment
        const ssize_t _surface_buf_bytes = SURFACE_BUF_COUNT * get_surface_buf_size(&_st_output->mode);
        const ssize_t _capture_buf_bytes = get_capture_buf_size(_st_output);
        const ssize_t _capture_buf_2_bytes = get_capture_buf_2_size(_st_output);
        // TODO: persistent libav allocations
        // selection: No manual allocations

        *shm_size_bytes += _surface_buf_bytes + _capture_buf_bytes + _capture_buf_2_bytes;
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
        g_state.globals.shm,
        global_pool_shm_fd,
        *shm_size_bytes
    );

    // Assign allocated memory
    ssize_t curr_offset = 0;
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
            _st_output->surface.double_buffer[i].data = *shm_addr + curr_offset;
            _st_output->surface.double_buffer[i].wl_buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                curr_offset,
                get_output_width_logical(_st_output),
                get_output_height_logical(_st_output),
                get_surface_stride(&_st_output->mode),
                SURFACE_SHM_FORMAT
            );
            curr_offset += get_surface_buf_size(&_st_output->mode);

            wl_buffer_add_listener(
                _st_output->surface.double_buffer[i].wl_buffer,
                &surface_buffer_listener,
                &_st_output->surface.double_buffer[i]
            );
        }

        _st_output->capture.frame_ctx.st_buffer.data = *shm_addr + curr_offset;
        _st_output->capture.frame_ctx.st_buffer.wl_buffer = wl_shm_pool_create_buffer(
            global_pool_wl,
            curr_offset,
            _st_output->mode.width_px,
            _st_output->mode.height_px,
            get_capture_stride(_st_output),
            _st_output->capture.shm_format
        );
        curr_offset += get_capture_buf_size(_st_output);

        _st_output->capture.frame_ctx.img_data_2 = *shm_addr + curr_offset;
        curr_offset += get_capture_buf_2_size(_st_output);

        wl_buffer_add_listener(
            _st_output->capture.frame_ctx.st_buffer.wl_buffer,
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
init_postmem()
{
    assert(g_state.n_outputs <= MAX_OUTPUTS);
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        if (!init_selection_and_blend2d(_st_output)) {
            return false;
        }

        // Initial frame callback request.
        // All subsequent requests are done "recursively" from within ::done
        wl_callback_add_listener(
            wl_surface_frame(_st_output->surface.wl_surface),
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
        //
        //       XXX NOTE: First we commit to get configure event (during init),
        //                 THEN we commit again (here) to "dispatch" the
        //                 event loop (the recursive frame callback).
        dispatch_surface_event_loop(_st_output);
    }

    return true;
}

static void
init_postmem__destroy()
{
    for (int i = 0; i < g_state.n_outputs; ++i) {
        destroy_selection_and_blend2d(&g_state.outputs[i]);
    }
}

// Init that should happen after all wayland-related init is finished
static bool
init_postwl()
{
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        if (!init_postwl__capture(_st_output)) {
            return false;
        }
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
    void *shm_addr = NULL;
    size_t shm_size_bytes = 0;

    if (!init_premem()) {
        eprintf("Failed pre-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }

    if (!init_meminit(&shm_addr, &shm_size_bytes)) {
        eprintf("Failed to initialize memory and/or shared memory buffers.\n");
        return EXIT_FAILURE;
    }

    if (!init_postmem()) {
        eprintf("Failed post-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }

    if (!init_postwl()) {
        eprintf("Failed post-wayland allocation initialization.\n");
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
        !g_state.exit_requested
        ||
        g_state.n_captures_in_progress > 0
    ) {
        const int _dispatch_status = wl_display_dispatch(g_state.globals.display);

        if (_dispatch_status == -1) {
            // TODO: Check errno and print/handle error
            eprintf("Error during wl_display_dispatch().\n");
            break;
        }
    };

    wl_display_dispatch_pending(g_state.globals.display);
    wl_display_roundtrip(g_state.globals.display);


    init_postmem__destroy();
    munmap(shm_addr, shm_size_bytes); // TODO: Put into init_meminit_destroy?
    init_premem__destroy();

    wl_display_disconnect(g_state.globals.display);
    eprintf("Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

