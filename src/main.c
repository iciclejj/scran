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

#include "state.h"
#include "state-util.h"
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


// XXX TODO: Rename this to g_scran or g_scran_state, probably.
struct scran g_state = { };


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


// Just bump this if/when we need more
#define _ARENA_BLOCKS_MAX (MAX_OUTPUTS * 3)

struct arena_context {
    void *addr;
    size_t size;

    size_t block_count;
    size_t block_offsets[_ARENA_BLOCKS_MAX];
    void **block_recipients[_ARENA_BLOCKS_MAX];
};

static inline void
_arena_add_block(
    struct arena_context *arena_ctx,
    int block_size,
    int block_alignment,
    void **block_pointer_recipient
) {
    const int block_idx = arena_ctx->block_count;
    const int old_size = arena_ctx->size;

    assert(block_idx < _ARENA_BLOCKS_MAX);

    arena_ctx->block_recipients[block_idx] = block_pointer_recipient;

    int bytes_past_alignment = old_size % block_alignment;
    int block_alignment_front_padding = bytes_past_alignment == 0 ? 0 : block_alignment - bytes_past_alignment;

    arena_ctx->block_offsets[block_idx] = old_size + block_alignment_front_padding;
    arena_ctx->size += block_alignment_front_padding + block_size;
    arena_ctx->block_count += 1;
}


// Open shm file, get fd, unlink file, return fd.
// The underlying file survives unlinking.
int
_shm_open_anon(void)
{
    static const char *shm_tmp_filename = "/icicle-wayland-client-jfkdsalfj";
    // TODO: Generate random filenames in case file already exists?
    int fd = shm_open(shm_tmp_filename, O_CREAT | O_RDWR | O_EXCL, 0600);

    if (fd >= 0) {
        shm_unlink(shm_tmp_filename);
    }

    return fd;
}

// TODO:
//  - Add --slim/--no-video arg that skips allocating video-only requirements,,
//    extra frame buffers, etc.
//  - persistent libav allocations
//  - selection: No manual allocations
static bool
init_meminit(
    struct arena_context *shm_arena_ctx
) {
    // XXX: This assert is not necessarily required for this function to run as it
    // should, assuming the context was properly set up until this point.
    assert(shm_arena_ctx->block_count == 0);


    //
    // Gather memory requirements
    //
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        // XXX: Handle this gracefully (and maybe in a nicer location?)
        if (_st_output->capture.shm_format == -1) {
            DEBUG("Failed to select shm_buffer format.\n");
            return false;
        }

        // TODO: Don't add padding for buffers we don't need it for
        //          - Makes things especially difficult and/or useless for
        //            buffers like the capture "source" buffers, provided by
        //            wayland, because we don't have control over the
        //            padding/stride/etc.

        for (int i_buffer = 0; i_buffer < SURFACE_BUF_COUNT; i_buffer++) {
            const ssize_t _surface_buf_size = get_surface_buf_size_padded(&_st_output->mode);
            _arena_add_block( shm_arena_ctx,
                _surface_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &_st_output->surface.double_buffer[i_buffer].data
            );
        };

        const ssize_t _capture_buf_size = get_capture_buf_size_padded(_st_output);
        _arena_add_block( shm_arena_ctx,
            _capture_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &_st_output->capture.frame_ctx.st_buffer.data
        );

        const ssize_t _capture_buf_2_size = get_capture_buf_2_size_padded(_st_output);
        _arena_add_block( shm_arena_ctx,
            _capture_buf_2_size, FRAMEBUFFER_ALIGNMENT_BYTES, &_st_output->capture.frame_ctx.img_data_2
        );
    }


    //
    // Get memory
    //
    const int global_pool_shm_fd = _shm_open_anon();
    if (global_pool_shm_fd == -1) {
        eprintf("Failed to open shared memory.\n");
        return false;
    }
    if (ftruncate(global_pool_shm_fd, shm_arena_ctx->size) == -1) {
        DEBUG("Failed to resize shm file to %zu\n", shm_arena_ctx->size);
        close(global_pool_shm_fd);
        return false;
    }
    shm_arena_ctx->addr = mmap(NULL, shm_arena_ctx->size, PROT_READ | PROT_WRITE, MAP_SHARED, global_pool_shm_fd, 0);
    madvise(shm_arena_ctx->addr, shm_arena_ctx->size, MADV_HUGEPAGE);
    // TODO: Only allocate what the server will actually need.
    //           F.ex., the server doesn't need to have libav objects.
    struct wl_shm_pool *global_pool_wl = wl_shm_create_pool(
        g_state.globals.shm,
        global_pool_shm_fd,
        shm_arena_ctx->size
    );


    //
    // Assign addresses
    //
    for (int i = 0; i < shm_arena_ctx->block_count; ++i) {
        *shm_arena_ctx->block_recipients[i] = shm_arena_ctx->addr + shm_arena_ctx->block_offsets[i];
    }


    //
    // Create wayland buffers
    //
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        for (int i_buffer = 0; i_buffer < SURFACE_BUF_COUNT; i_buffer++) {
            struct scran_output_surface_buffer *_st_buffer = &_st_output->surface.double_buffer[i_buffer];

            assert(_st_buffer->data != NULL);
            const ptrdiff_t _surface_buffer_offset = _st_buffer->data - shm_arena_ctx->addr;
            _st_buffer->wl_buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                _surface_buffer_offset,
                get_output_width_logical(_st_output),
                get_output_height_logical(_st_output),
                get_surface_stride(&_st_output->mode),
                SURFACE_SHM_FORMAT
            );

            wl_buffer_add_listener(
                _st_buffer->wl_buffer,
                &surface_buffer_listener,
                _st_buffer
            );
        }

        assert(_st_output->capture.frame_ctx.st_buffer.data != NULL);
        const ptrdiff_t _capture_buffer_offset = _st_output->capture.frame_ctx.st_buffer.data - shm_arena_ctx->addr;
        _st_output->capture.frame_ctx.st_buffer.wl_buffer = wl_shm_pool_create_buffer(
            global_pool_wl,
            _capture_buffer_offset,
            _st_output->mode.width_px,
            _st_output->mode.height_px,
            get_capture_stride(_st_output),
            _st_output->capture.shm_format
        );

        wl_buffer_add_listener(
            _st_output->capture.frame_ctx.st_buffer.wl_buffer,
            &capture_buffer_listener,
            &_st_output->capture.frame_ctx.st_buffer
        );
    }

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


// TODO: Allow selection before capture protocols are ready?
//           Probably negligible and difficult without multithreading
//       Probably find a cleaner way to do this multi-step init?
//       Remember to fix off-by-one bug when selecting corner to corner
//           F.ex. 2559x1599 rect width/height
int main(void)
{
    struct arena_context arena_ctx = { };

    if (!init_premem()) {
        eprintf("Failed pre-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }

    if (!init_meminit(&arena_ctx)) {
        eprintf("Failed to initialize memory and/or shared memory buffers.\n");
        return EXIT_FAILURE;
    }

    if (!init_postmem()) {
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
    munmap(arena_ctx.addr, arena_ctx.size); // TODO: Put into init_meminit_destroy?
    init_premem__destroy();

    wl_display_disconnect(g_state.globals.display);
    eprintf("Disconnected from wayland server (%s)\n", SOCKNAME);

    return 0;
}

