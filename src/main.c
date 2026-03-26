#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/epoll.h>

#include <linux/input-event-codes.h>

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "selection.h"
#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "init.h"
#include "print.h"
#include "options.h"
#include "signal-handlers.h"
#include "xdg-output-unstable-v1.h"

//  General TODO:
//  - Don't use libwayland..? Handle its allocations etc. ourselves?
//  - Do we need error handling for all the various wayland functions?
//      - Probably worthwhile during init
//

// TODO: Dynamically find this name
#define SOCKNAME "wayland-1"
#define SOCKPATH "/run/user/1000/" SOCKNAME


struct scran g_state = {
    .options = {
        .filename_format = SCRAN_OUTPUT_FILENAME_FORMATSTRING_DEFAULT,
        .output_path = SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH,
        .output_path_filename_pointer = g_state.options.output_path
                                        + sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH)
                                        - 1, // Null-terminator
    },
};


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
    wl_registry_add_listener(g_state.globals.registry, &registry_listener, &g_state);
    wl_display_roundtrip(g_state.globals.display);
    DEBUG("Roundtripped after adding registry listener()\n");

    // TODO: Validate globals (at least non-stable protocols)

    if (g_state.n_outputs < 1) {
        eprintf("No outputs detected.\n");
        return false;
    }

    //   Collect dynamic memory requirements
    // + Initialize otherwhat lacking extra dependencies (beyond globals)
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        if (!init_premem__selection(_st_output, &g_state.globals)) {
            return false;
        }

        if (!init_premem__capture(_st_output, &g_state.seat.datacontrol, &g_state.globals)) {
            return false;
        }

        _st_output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
            g_state.globals.xdg_output_manager, _st_output->wl_output
        );

        zxdg_output_v1_add_listener(_st_output->xdg_output, &xdg_output_listener, _st_output);
    }

    // We don't need this anymore unless we want to support live geometry updates
    zxdg_output_manager_v1_destroy(g_state.globals.xdg_output_manager);

    g_state.empty_wl_region = wl_compositor_create_region(g_state.globals.compositor);

    // Then roundtrip to collect listener-provided memory requirements into our state struct.
    // This should be our last required roundtrip until the main event loop dispatch.
    wl_display_roundtrip(g_state.globals.display);

    return true;
}

static inline void
_stay_alive_while_clipboard_active()
{
    atomic_int *clipboard_refcount = &g_state.seat.datacontrol.selection_refcount;
    assert(*clipboard_refcount >= 0);

    if (*clipboard_refcount > 0) {
        eprintf("Keeping clipboard selection alive until stolen...\n");

        while (*clipboard_refcount > 0) {
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

        init_premem__selection__destroy(_st_output);
        init_premem__capture__destroy(_st_output);

        // XXX: This could be probably be destroyed within output::done if we
        //      we won't support live updating. Keeping it here for now.
        //      NOTE: Use output::done for xdg_output <v3, and xdg_output::done
        //            for v3 (see xml)
        zxdg_output_v1_destroy(_st_output->xdg_output);
    }

    wl_region_destroy(g_state.empty_wl_region);

    // TODO: Make sure this happens at an appropriate point in time (memory
    // footprint should be minimized), once the init/cleanup is more
    // finalized.
    if (!g_state.options.no_keepalive) {
        _stay_alive_while_clipboard_active();
    }

    registry_listener__destroy(&g_state);
}


// Just bump this if/when we need more
#define _ARENA_BLOCKS_MAX (MAX_OUTPUTS * 3)

struct _arena_context {
    void *addr;
    size_t size;

    size_t block_count;
    size_t block_offsets[_ARENA_BLOCKS_MAX];
    void **block_recipients[_ARENA_BLOCKS_MAX];
};

static inline void
_arena_add_block(
    struct _arena_context *restrict arena_ctx,
    size_t block_size,
    size_t block_alignment,
    void **block_pointer_recipient
) {
    const size_t i_block = arena_ctx->block_count;
    const size_t arena_size_old = arena_ctx->size;

    assert(i_block < _ARENA_BLOCKS_MAX);

    const size_t block_pre_padding = get_required_padding(arena_size_old, block_alignment);

    arena_ctx->block_offsets[i_block]
        = arena_size_old + block_pre_padding;
    arena_ctx->size
        = arena_size_old + block_pre_padding + block_size;

    arena_ctx->block_recipients[i_block] = block_pointer_recipient;
    arena_ctx->block_count += 1;
}

static inline void
_arena_hand_out_pointers_to_recipients(struct _arena_context *arena) {
    assert(arena->addr != NULL);

    for (int i = 0; i < arena->block_count; ++i) {
        *arena->block_recipients[i] = arena->addr + arena->block_offsets[i];
    }
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
//  - Add --slim/--no-video arg that skips allocating video-only requirements,
//    extra frame buffers, etc.
//  - persistent libav allocations
//  - selection: No manual allocations
static bool
init_meminit(
    struct _arena_context *shm_arena,
    struct _arena_context *private_arena
) {
    // XXX: These asserts are not necessarily required for this function to run
    // as it should, assuming the context was properly set up until this point.
    assert(shm_arena->block_count == 0);
    assert(private_arena->block_count == 0);


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
            const size_t _surface_buf_size = get_surface_buf_size_padded(&_st_output->mode);
            _arena_add_block(
                shm_arena,
                _surface_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &_st_output->surface.double_buffer[i_buffer].data
            );
        };

        const size_t _capture_buf_size = get_capture_buf_size_padded(_st_output);
        _arena_add_block(
            shm_arena,
            _capture_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &_st_output->capture.frame_ctx.st_buffer.data
        );

        const size_t _capture_buf_2_size = get_capture_buf_2_size_padded(_st_output);
        _arena_add_block(
            private_arena,
            _capture_buf_2_size, FRAMEBUFFER_ALIGNMENT_BYTES, &_st_output->capture.frame_ctx.img_data_2
        );
    }


    //
    // Get shared memory
    //
    // TODO: Hugepages? (Shared memory must request it through madvise.)
    const int global_pool_shm_fd = _shm_open_anon();
    if (global_pool_shm_fd == -1) {
        eprintf("Failed to open shared memory.\n");
        return false;
    }
    if (ftruncate(global_pool_shm_fd, shm_arena->size) == -1) {
        DEBUG("Failed to resize shm file to %zu\n", shm_arena->size);
        close(global_pool_shm_fd);
        return false;
    }
    shm_arena->addr = mmap(NULL, shm_arena->size, PROT_READ | PROT_WRITE, MAP_SHARED, global_pool_shm_fd, 0);
    struct wl_shm_pool *global_pool_wl = wl_shm_create_pool(
        g_state.globals.shm,
        global_pool_shm_fd,
        shm_arena->size
    );


    //
    // Get private memory
    //
    private_arena->addr = mmap(NULL, private_arena->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);


    // TODO: Assert alignments

    //
    // Assign addresses
    //
    _arena_hand_out_pointers_to_recipients(shm_arena);
    _arena_hand_out_pointers_to_recipients(private_arena);


    //
    // Create wayland buffers
    //
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

        for (int i_buffer = 0; i_buffer < SURFACE_BUF_COUNT; i_buffer++) {
            struct scran_output_surface_buffer *_st_buffer = &_st_output->surface.double_buffer[i_buffer];

            assert(_st_buffer->data != NULL);
            const ptrdiff_t _surface_buffer_offset = _st_buffer->data - shm_arena->addr;
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
        const ptrdiff_t _capture_buffer_offset = _st_output->capture.frame_ctx.st_buffer.data - shm_arena->addr;
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

        if (!init_postmem__selection(_st_output)) {
            return false;
        }
    }

    if (g_state.options.have_custom_initial_selection) {
        struct BLBoxI        custom_selection;
        struct scran_output *custom_selection_output = NULL;

        global_rect_to_output_box_clamped(
            g_state.options.custom_initial_selection_global_coordinates,
            &custom_selection,
            &custom_selection_output
        );

        if (custom_selection_output == NULL) {
            eprintf("Error: Top left corner of geometry string is not within any detected output.\n");
            return false;
        }

        custom_selection_output->selection_ctx.bl_box = custom_selection;

        signal_selection_initialized(custom_selection_output);
    }

    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *_st_output = &g_state.outputs[i];

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
        dispatch_selection_surface_event_loop(_st_output);
    }

    return true;
}

static void
init_postmem__destroy()
{
    for (int i = 0; i < g_state.n_outputs; ++i) {
        init_postmem__selection__destroy(&g_state.outputs[i]);
    }
}


struct _scran_signal_masks {
    sigset_t original;
    sigset_t with_scran_handlers_unmasked;
    sigset_t with_scran_handlers_masked;
};

static inline bool
_init_signal(int signo, struct sigaction *sigaction_, sigset_t *mask_inplace, sigset_t *unmask_inplace)
{
    // INFO: A sigaction.sa_mask is for signals blocked during handler
    // execution, and is separate from our global block mask (that we
    // use for epoll_pwait). The signal that triggered the handler is
    // blocked by default, even with an empty mask (unless SA_NODEFER).
    sigemptyset(&sigaction_->sa_mask); 

    assert(sigaction_->sa_handler != NULL);

    if (sigaction(signo, sigaction_, NULL) == -1) {
        eprintf("sigaction failed (signal number: %d): %s\n",
                signo, strerror(errno));
        return false;
    }

    sigaddset(mask_inplace, signo);
    sigdelset(unmask_inplace, signo);

    return true;
}

static bool
init_signals(struct _scran_signal_masks *masks)
{
    // Get already blocked signals
    // TODO: Is getting the old mask actually worthwhile? Also, if we do care
    // about the old mask, e.g. one set before a parent's exec, then maybe just
    // warn about blocked handlers, rather than simply unblocking them?
    if (sigprocmask(SIG_SETMASK, NULL, &masks->original)) {
        eprintf("sigprocmask failed: %s\n", strerror(errno));
        return false;
    }
    masks->with_scran_handlers_unmasked = masks->original;
    masks->with_scran_handlers_masked = masks->original;

    static struct sigaction sa_grab_focus_unsafe = { .sa_handler = sig_grab_focus };
    if (!_init_signal(SIGUSR1, &sa_grab_focus_unsafe, &masks->with_scran_handlers_masked, &masks->with_scran_handlers_unmasked)) {
        return false;
    }

    return true;
}


// TODO: More asserts
// epoll_fd_out: closed by caller to make error handling simpler
static bool
run_main_loop(int *const restrict epoll_fd_out, struct _scran_signal_masks *signal_masks)
{
    // TODO: Verify whether that this isn't redundant
    wl_display_flush(g_state.globals.display);

    int wl_display_fd = wl_display_get_fd(g_state.globals.display);
    const int epoll_fd = epoll_create(1); // TODO: O_CLOEXEC if threading
    if (epoll_fd == -1) {
        eprintf("epoll_create() failed: %s\n", strerror(errno));
        return false;
    }
    *epoll_fd_out = epoll_fd;

    struct epoll_event wl_display_epoll_event = { .events = EPOLLIN };
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wl_display_fd, &wl_display_epoll_event);

    // Block/defer our signal handlers until explicit unblock during epoll
    if (sigprocmask(SIG_BLOCK, &signal_masks->with_scran_handlers_masked, NULL) == -1) {
        eprintf("sigprocmask failed: %s\n", strerror(errno));
        return false;
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
        while (wl_display_prepare_read(g_state.globals.display) != 0) {
            if (wl_display_dispatch_pending(g_state.globals.display) == -1) {
                // TODO: Check errno and print/handle error better
                eprintf("Error during wl_display_dispatch().\n");
                wl_display_cancel_read(g_state.globals.display);
                return false;
            }
        }
        wl_display_flush(g_state.globals.display);

        // Let blocked unsafe signals fire synchronously here
        if (-1 == epoll_pwait(epoll_fd, &wl_display_epoll_event, 1, -1, &signal_masks->with_scran_handlers_unmasked)) {
            wl_display_cancel_read(g_state.globals.display);
            if (errno != EINTR) {
                // TODO: Read errno
                eprintf("Error during epoll_pwait().\n");
                return false;
            }
        } else {
            // TODO: Handle error (-1) ?
            wl_display_read_events(g_state.globals.display);
        }

        if (g_state.sig_focus_requested == true) {
            start_grabbing_focus();
            // TODO: Make this a tiered enum to prevent handling it twice?
            g_state.sig_focus_requested = false;
        }

        wl_display_dispatch_pending(g_state.globals.display);
    };

    return true;
}

// TODO: Allow selection before capture protocols are ready?
//           Probably negligible benefit for the added complexity
//       goto cleanup, rather than EXIT_FAILURE?
int main(int argc, char *argv[])
{
    if (!scran_handle_args(argc, argv)) {
        return EXIT_FAILURE;
    }


    struct _arena_context shm_arena = { };
    struct _arena_context private_arena = { };

    if (!init_premem()) {
        eprintf("Failed pre-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }

    if (!init_meminit(&shm_arena, &private_arena)) {
        eprintf("Failed to initialize memory and/or shared memory buffers.\n");
        return EXIT_FAILURE;
    }

    if (!init_postmem()) {
        eprintf("Failed post-memory allocation initialization.\n");
        return EXIT_FAILURE;
    }

    struct _scran_signal_masks signal_masks;
    if (!init_signals(&signal_masks)) {
        eprintf("Failed to initialize signals.\n");
        return EXIT_FAILURE;
    }


    int epoll_fd = -1; // Closed by caller

    if (!run_main_loop(&epoll_fd, &signal_masks)) {
        eprintf("Main loop returned with error. Attempting normal cleanup.\n");
    }

    close(epoll_fd);



    wl_display_dispatch_pending(g_state.globals.display);
    wl_display_roundtrip(g_state.globals.display);


    init_postmem__destroy();
    assert(shm_arena.addr != NULL && private_arena.addr != NULL);
    munmap(shm_arena.addr, shm_arena.size); // TODO: Put into init_meminit__destroy?
    munmap(private_arena.addr, private_arena.size); // TODO: Put into init_meminit__destroy?
    init_premem__destroy();

    wl_display_disconnect(g_state.globals.display);
    eprintf("Disconnected from wayland server (%s)\n", SOCKNAME);

    eprintf("Scran exited successfully.\n");

    return 0;
}

