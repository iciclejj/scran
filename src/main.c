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
#ifdef __GLIBC__
  // for malloc_trim() of other libraries' mallocs (mainly ffmpeg) before
  // background keepalive
  #include <malloc.h>
#endif

#include <linux/input-event-codes.h>

#include <wayland-client-core.h>
#include <wayland-client.h>
#include <blend2d/blend2d.h>

#include "wlr-output-management-unstable-v1.h"
#include "xdg-output-unstable-v1.h"

#include "scranrot.h"

#include "selection.h"
#include "state.h"
#include "state-util.h"
#include "event-handlers.h"
#include "init.h"
#include "print.h"
#include "options.h"
#include "signal-handlers.h"
#include "util/blend2d.h"
#include "portals.h"
#include "pipewires.h"
#include "scranrot.h"

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

#define SCRAN_EPOLL_SIZE 3 // XXX: hardcoded: dbus/portal + wayland + pipewire
static struct epoll_event m_epoll_events[SCRAN_EPOLL_SIZE];
static int m_epoll_fd = -1;


// Initialize:
//   - Wayland globals
//   - init_meminit() dependencies
//   - Otherwhat that might benefit from early init
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


    // Collect dynamic memory requirements
    // + otherwhat that might benefit from early init

    // We need this early for freezeframe init
    g_state.empty_wl_region = wl_compositor_create_region(g_state.globals.compositor);

    FOR_EACH_OUTPUT(i, st_output) {
        if (!init_premem__capture(st_output, &g_state.seat.datacontrol, &g_state.globals)) {
            return false;
        }

        // NOTE: Must initialized be prior to selection, since they're both on
        // the same layer-shell layer.
        if (g_state.options.freezeframe) {
            if (!init_premem__freezeframe(st_output)) {
                return false;
            }
        }

        if (!init_premem__selection(st_output, &g_state.globals)) {
            return false;
        }

        st_output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
            g_state.globals.xdg_output_manager, st_output->wl_output
        );

        zxdg_output_v1_add_listener(st_output->xdg_output, &xdg_output_listener, st_output);
    }

    if (!init_premem__datacontrol(&g_state.seat.datacontrol)) {
        return false;
    }

    // We don't need this anymore unless we want to support live geometry updates
    zxdg_output_manager_v1_destroy(g_state.globals.xdg_output_manager);

    // NOTE: We only use this protocol for COSMIC-specific scaling fixes, at
    // the moment. Change to it through (and also update accordingly within
    // wlr_output.c), if necessary.
    if (g_state.globals.cosmic_output_manager && g_state.globals.wlr_output_manager) {
        DEBUG("Adding wlr_output_manager listener\n");
        zwlr_output_manager_v1_add_listener(g_state.globals.wlr_output_manager, &wlr_output_manager_listener, &g_state);
    }

    // Then roundtrip to collect listener-provided memory requirements into our state struct.
    wl_display_roundtrip(g_state.globals.display);

    // Then roundtrip again for our collected wlr_output_head instances
    // (from wlr_output_manager::head)...
    //     TODO: This should probably be awaited more properly?
    // This should be our last required roundtrip until the main event loop dispatch.
    wl_display_roundtrip(g_state.globals.display);

    return true;
}

static inline void
_stay_alive_while_clipboard_active()
{

#ifdef __GLIBC__
    // ffmpeg's internal mallocs can hold onto significant amounts of memory
    // even after all contexts etc. are destroyed.
    //
    // Keeping a page of memory available for basu/wayland-client
    //     TODO: Verify that this is a reasonable amount of memory to keep.
    //     We'd probably want to keep enough for an image paste to go through
    //     without new allocations? (Our image buffers stay in our own arena.)
    malloc_trim(4096);
    DEBUG("Trimmed memory\n");
#endif

    atomic_int *clipboard_refcount = &g_state.seat.datacontrol.selection_refcount;
    assert(*clipboard_refcount >= 0);

    if (*clipboard_refcount < 1) {
        return;
    }

    eprintf("Keeping clipboard selection alive until stolen...\n");

    int wl_display_fd = wl_display_get_fd(g_state.globals.display);
    int scran_portal_timeout_ms;
    scran_portal_update(m_epoll_fd, &scran_portal_timeout_ms);

    while (*clipboard_refcount > 0) {
        while (wl_display_prepare_read(g_state.globals.display) != 0) {
            if (wl_display_dispatch_pending(g_state.globals.display) == -1) {
                // TODO: Check errno and print/handle error better
                eprintf("Error during wl_display_dispatch().\n");
                wl_display_cancel_read(g_state.globals.display);
                return;
            }
        }
        wl_display_flush(g_state.globals.display);

        int ret = epoll_wait(m_epoll_fd, m_epoll_events, SCRAN_EPOLL_SIZE, scran_portal_timeout_ms);

        if (ret < 0) {
            wl_display_cancel_read(g_state.globals.display);

            if (errno != EINTR) {
                // TODO: Read errno
                eprintf("Error during epoll_wait().\n");
                return;
            }
        } else {
            bool wayland_ready = false;
            for (int i = 0; i < ret; ++i) {
                if (m_epoll_events[i].data.fd == wl_display_fd) {
                    wayland_ready = true;
                }
            }
            if (wayland_ready) {
                if (-1 == wl_display_read_events(g_state.globals.display)) {
                    eprintf("Wayland error %d: %s\n", ret, strerror(errno));
                    return;
                }
                wl_display_dispatch_pending(g_state.globals.display);
            } else {
                wl_display_cancel_read(g_state.globals.display);
            }
        }

        // Sanity check (should only loop in response to actions)
        DEBUG("Looped in keepalive\n");

        // Fire this unconditionally after every poll (details in function description)
        // TODO: Can we somehow find out whether a notification has been
        // dismissed by the user, so we can stop polling this, now that no new
        // notifications can be launched anymore (during clipboard keepalive)?
        scran_portal_update(m_epoll_fd, &scran_portal_timeout_ms);
    }

    eprintf("Clipboard selection stolen! Continuing exit.\n");
}

static void
init_premem__destroy()
{
    registry_listener__destroy(&g_state);

    FOR_EACH_OUTPUT(i, st_output) {
        init_premem__selection__destroy(st_output);
        init_premem__capture__destroy(st_output);
        if (g_state.options.freezeframe) {
            init_premem__freezeframe__destroy(st_output);
        }

        // XXX: This could be probably be destroyed within output::done if we
        //      we won't support live updating. Keeping it here for now.
        //      NOTE: Use output::done for xdg_output <v3, and xdg_output::done
        //            for v3 (see xml)
        zxdg_output_v1_destroy(st_output->xdg_output);
    }

    wl_region_destroy(g_state.empty_wl_region);

    // NOTE: Make sure this stays at an appropriate place in the teardown
    // sequence, so that memory footprint is minimized.
    if (!g_state.options.no_keepalive) {
        _stay_alive_while_clipboard_active();
    }

    init_premem__datacontrol__destroy(&g_state.seat.datacontrol);
}


// Just bump this if/when we need more
// TODO: Make this cleaner...
#define _ARENA_BLOCKS_MAX (MAX_OUTPUTS * 6)

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

    const size_t block_pre_padding = get_units_until_alignment(arena_size_old, block_alignment);

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

    for (size_t i = 0; i < arena->block_count; ++i) {
        *arena->block_recipients[i] = arena->addr + arena->block_offsets[i];
    }
}

// Open shm file, get fd, unlink file, return fd.
// The underlying file survives unlinking.
static inline int
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
    FOR_EACH_OUTPUT(i, st_output) {
        // XXX: Handle this gracefully (and maybe in a nicer location?)
        if (st_output->capture.shm_format == SCRAN_SHM_FORMAT_UNSET) {
            DEBUG("Failed to select shm_format for capture buffer.\n");
            return false;
        }

        for (int i_buffer = 0; i_buffer < SELECTION_SURFACE_BUF_COUNT; i_buffer++) {
            const size_t _surface_buf_size = get_surface_buf_size_padded(&st_output->selection_surface.surface);
            _arena_add_block(
                shm_arena,
                _surface_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &st_output->selection_surface.double_buffer[i_buffer].data
            );
        };

        if (g_state.options.freezeframe) {
            if (st_output->freezeframe.shm_format == SCRAN_SHM_FORMAT_UNSET) {
                DEBUG("Failed to select shm_format for freezeframe capture buffer.\n");
                return false;
            }

            // TODO: Maybe do proper ::released handling for these buffers, but
            // has not caused issues yet, since we're effectively guaranteed
            // multiple frames between each freezeframe refresh

            // XXX: We use a separate capture buffer and surface buffer due to
            // wl_surface::set_buffer_transform not working as expected in
            // Hyprland (#14441).
            const size_t _capture_buf_size = get_capture_buf_size(st_output);
            _arena_add_block(
                shm_arena,
                _capture_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &st_output->freezeframe.capture_buffer.data
            );
            _arena_add_block(
                shm_arena,
                _capture_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &st_output->freezeframe.surface_buffer.data
            );

            // single-pixel buffer
            const size_t _transparent_buf_size = RGBA32_PIXEL_STRIDE;
            _arena_add_block(
                shm_arena,
                _transparent_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &st_output->freezeframe.transparent_single_pixel_buffer.data
            );
        }

        const size_t _capture_buf_size = get_capture_buf_size(st_output);
        _arena_add_block(
            shm_arena,
            _capture_buf_size, FRAMEBUFFER_ALIGNMENT_BYTES, &st_output->capture.frame_ctx.st_buffer.data
        );

        const size_t _capture_buf_2_size = get_capture_buf_size(st_output);
        _arena_add_block(
            private_arena,
            _capture_buf_2_size, FRAMEBUFFER_ALIGNMENT_BYTES, &st_output->capture.frame_ctx.img_data_2
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
    FOR_EACH_OUTPUT(i, st_output) {
        struct scran_output_selectionSurface *_selection_surface = &st_output->selection_surface;
        for (int i_buffer = 0; i_buffer < SELECTION_SURFACE_BUF_COUNT; i_buffer++) {
            struct scran_output_selectionSurface_buffer *_st_buffer = &_selection_surface->double_buffer[i_buffer];

            assert(_st_buffer->data != NULL);
            const ptrdiff_t _surface_buffer_offset = _st_buffer->data - shm_arena->addr;
            _st_buffer->wl_buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                _surface_buffer_offset,
                _selection_surface->surface.width_px_buffer,
                _selection_surface->surface.height_px_buffer,
                _selection_surface->surface.width_px_buffer * SURFACE_PIXEL_STRIDE,
                SURFACE_SHM_FORMAT
            );

            wl_buffer_add_listener(
                _st_buffer->wl_buffer,
                &selectionSurface_buffer_listener,
                _st_buffer
            );
        }

        if (g_state.options.freezeframe) {
            struct scran_output_freezeframe *_freezeframe_surface = &st_output->freezeframe;

            struct scran_output_freezeframe_buffer *_capture_buffer = &_freezeframe_surface->capture_buffer;
            const ptrdiff_t _capture_buffer_offset = _capture_buffer->data - shm_arena->addr;
            _capture_buffer->wl_buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                _capture_buffer_offset,
                st_output->mode.width_px,
                st_output->mode.height_px,
                get_capture_stride(st_output),
                _freezeframe_surface->shm_format
            );

            struct scran_output_freezeframe_buffer *_surface_buffer = &_freezeframe_surface->surface_buffer;
            const ptrdiff_t _surface_buffer_offset = _surface_buffer->data - shm_arena->addr;
            _surface_buffer->wl_buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                _surface_buffer_offset,
                _freezeframe_surface->surface.width_px_buffer,
                _freezeframe_surface->surface.height_px_buffer,
                _freezeframe_surface->surface.width_px_buffer * SURFACE_PIXEL_STRIDE,
                _freezeframe_surface->shm_format
            );

            struct scran_output_freezeframe_buffer *_transparent_buffer = &_freezeframe_surface->transparent_single_pixel_buffer;
            const ptrdiff_t _transparent_buffer_offset = _transparent_buffer->data - shm_arena->addr;
            _transparent_buffer->wl_buffer = wl_shm_pool_create_buffer(
                global_pool_wl,
                _transparent_buffer_offset,
                // single-pixel buffer
                1, 1, SURFACE_PIXEL_STRIDE,
                // TODO: Assert this has alpha channel? Though we have bigger
                // problems if that ever fails...
                SURFACE_SHM_FORMAT
            );
        }

        assert(st_output->capture.frame_ctx.st_buffer.data != NULL);
        const ptrdiff_t _capture_buffer_offset = st_output->capture.frame_ctx.st_buffer.data - shm_arena->addr;
        st_output->capture.frame_ctx.st_buffer.wl_buffer = wl_shm_pool_create_buffer(
            global_pool_wl,
            _capture_buffer_offset,
            st_output->mode.width_px,
            st_output->mode.height_px,
            get_capture_stride(st_output),
            st_output->capture.shm_format
        );

        wl_buffer_add_listener(
            st_output->capture.frame_ctx.st_buffer.wl_buffer,
            &capture_buffer_listener,
            &st_output->capture.frame_ctx.st_buffer
        );
    }

    // (wayland's mmaps and fd references live on)
    wl_shm_pool_destroy(global_pool_wl);
    close(global_pool_shm_fd);

    DEBUG("Finished meminit\n");

    return true;
}

static void
init_meminit__destroy(
    struct _arena_context *shm_arena,
    struct _arena_context *private_arena
) {
    FOR_EACH_OUTPUT(i, st_output) {
        for (int i_buf = 0; i_buf < SELECTION_SURFACE_BUF_COUNT; i_buf++) {
            struct scran_output_selectionSurface_buffer *selection_surface_buffer = &st_output->selection_surface.double_buffer[i_buf];
            wl_buffer_destroy(selection_surface_buffer->wl_buffer);
        }

        {
            struct scran_capture_buffer *capture_buffer = &st_output->capture.frame_ctx.st_buffer;
            wl_buffer_destroy(capture_buffer->wl_buffer);
        }

        if (g_state.options.freezeframe) {
            wl_buffer_destroy(st_output->freezeframe.capture_buffer.wl_buffer);
            wl_buffer_destroy(st_output->freezeframe.surface_buffer.wl_buffer);
            wl_buffer_destroy(st_output->freezeframe.transparent_single_pixel_buffer.wl_buffer);
        }
    }

    assert(shm_arena->addr != NULL && private_arena->addr != NULL);
    munmap(shm_arena->addr, shm_arena->size);
    munmap(private_arena->addr, private_arena->size);
}

static bool
init_postmem()
{
    struct BLBoxI       custom_initial_selection;
    struct scran_output *custom_initial_selection_output = NULL;

    if (g_state.options.have_custom_initial_selection) {
        struct BLRectI      custom_initial_selection_rect;
        global_logical_rect_to_selection(
            g_state.options.custom_initial_selection_global_coordinates,
            &custom_initial_selection_rect,
            &custom_initial_selection_output
        );

        if (custom_initial_selection_output == NULL) {
            eprintf("Error: Top left corner of geometry string is not within any detected output.\n");
            return false;
        }

        custom_initial_selection = blrecti_to_blboxi(custom_initial_selection_rect);

        scran_ui_set_selection_stage_defaults(&custom_initial_selection_output->selection_surface.ui_ctx);
        set_selection_initialized(custom_initial_selection_output);
    }

    FOR_EACH_OUTPUT(i, st_output) {
        struct BLBoxI *_p_custom_initial_selection =
            (st_output == custom_initial_selection_output)
            ? &custom_initial_selection : NULL;

        if (g_state.options.freezeframe) {
            if (!init_postmem__freezeframe(st_output)) {
                return false;
            }
        }

        if (!init_postmem__selection(st_output, _p_custom_initial_selection)) {
            return false;
        }
    }

    return true;
}

static void
init_postmem__destroy()
{
    FOR_EACH_OUTPUT(i, st_output) {
        init_postmem__selection__destroy(st_output);

        if (g_state.options.freezeframe) {
            init_postmem__freezeframe__destroy(st_output);
        }
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
static bool
run_main_loop(struct _scran_signal_masks *signal_masks)
{
    assert(m_epoll_fd != -1);

    // TODO: Verify whether that this isn't redundant
    wl_display_flush(g_state.globals.display);

    int wl_display_fd = wl_display_get_fd(g_state.globals.display);
    struct epoll_event wl_display_epoll_event = {
        .events = EPOLLIN,
        .data.fd = wl_display_fd
    };
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, wl_display_fd, &wl_display_epoll_event);

    // Only our portal fd actually cares about max timeout atm
    int scran_portal_timeout_ms = -1;
    if (!scran_portal_init(m_epoll_fd, &scran_portal_timeout_ms)) {
        eprintf("WARNING: Failed to initialize XDG Desktop Portals\n");
        assert(scran_portal_timeout_ms == -1);
    }

    scran_pipewire_pre_init(m_epoll_fd);

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
        int ret = epoll_pwait(m_epoll_fd, m_epoll_events, SCRAN_EPOLL_SIZE, scran_portal_timeout_ms, &signal_masks->with_scran_handlers_unmasked);

        if (ret < 0) {
            wl_display_cancel_read(g_state.globals.display);

            if (errno != EINTR) {
                // TODO: Read errno
                eprintf("Error during epoll_pwait().\n");
                return false;
            }
        } else {
            bool wayland_ready = false;
            for (int i = 0; i < ret; ++i) {
                int ready_fd = m_epoll_events[i].data.fd;
                if (ready_fd == wl_display_fd) {
                    wayland_ready = true;
                } else {
                    // XXX: This could be nicer. It currently checks its own fd.
                    scran_pipewire_update(ready_fd);
                }
            }
            if (wayland_ready) {
                if (-1 == wl_display_read_events(g_state.globals.display)) {
                    eprintf("Wayland error %d: %s\n", ret, strerror(errno));
                    return false;
                }
                wl_display_dispatch_pending(g_state.globals.display);
            } else {
                wl_display_cancel_read(g_state.globals.display);
            }
        }

        // Fire this unconditionally after every poll (details in function description)
        scran_portal_update(m_epoll_fd, &scran_portal_timeout_ms);

        if (g_state.sig_focus_requested == true) {
            start_grabbing_focus();
            // TODO: Make this a tiered enum to prevent handling it twice?
            g_state.sig_focus_requested = false;
        }
    };

    scran_pipewire_destroy();

    return true;
}

// TODO: goto cleanup, rather than EXIT_FAILURE?
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

    scranrot_init();

    struct _scran_signal_masks signal_masks;
    if (!init_signals(&signal_masks)) {
        eprintf("Failed to initialize signals.\n");
        return EXIT_FAILURE;
    }



    m_epoll_fd = epoll_create(1); // TODO: O_CLOEXEC if threading
    if (m_epoll_fd == -1) {
        eprintf("epoll_create() failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (!run_main_loop(&signal_masks)) {
        eprintf("Main loop returned with error. Attempting normal cleanup.\n");
    }



    wl_display_dispatch_pending(g_state.globals.display);
    wl_display_roundtrip(g_state.globals.display);


    init_postmem__destroy();
    init_meminit__destroy(&shm_arena, &private_arena);
    init_premem__destroy();

    // TODO: Implement scran_portal_drain() to call here? Shouldn't really be
    // necessary in practice, at the moment.
    scran_portal_destroy(m_epoll_fd);
    close(m_epoll_fd);

    wl_display_disconnect(g_state.globals.display);
    eprintf("Disconnected from wayland server (%s)\n", SOCKNAME);

    eprintf("Scran exited successfully.\n");

    return 0;
}

