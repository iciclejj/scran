#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
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
#include "lib_interop.h"

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
    //       Account for scale/transform
    st_surface->buf_size = SURFACE_BYTES_PER_PIXEL * st_surface->width * st_surface->height;
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
            SURFACE_BYTES_PER_PIXEL * st_surface->width,
            SURFACE_SHM_FORMAT
        );

        wl_buffer_add_listener(
            st_surface->double_buffer[i].buffer,
            &buffer_listener,
            &st_surface->double_buffer[i]
        );
    }

    close(shm_fd);
    // TODO: Defer this until end of program execution?
    //           Decide for this and other destroys
    //           Else don't save shm_pool or shm_pool_size anywhere
    wl_shm_pool_destroy(st_surface->shm_pool);

    // TODO: Do this cleaner?
    for (int i = 0; i < BUF_COUNT; ++i) {
        if (st_surface->double_buffer[i].data == NULL) {
            return false;
        }
    }

    // TODO: Should this be done here?
    wl_surface_attach(st_surface->surface, st_surface->double_buffer[0].buffer, 0, 0);

    return true;
}

static inline void
destroy_surface_shm_buffers(struct client_state_surface *st_surface)
{
    for (int i = 0; i < BUF_COUNT; ++i) {
        wl_buffer_destroy(st_surface->double_buffer[i].buffer);
    }
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
            SURFACE_SHM_FORMAT_BL,
            st_buffer->data,
            SURFACE_BYTES_PER_PIXEL * state->surface.width,
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
init_image_copy_capture_frame(struct client_state *state)
{
    state->capture.frame = ext_image_copy_capture_session_v1_create_frame(
        state->capture.session
    );

    ext_image_copy_capture_frame_v1_add_listener(
        state->capture.frame,
        &image_copy_capture_frame_listener,
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
        state->surface.width,
        state->surface.height,
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

static inline bool
dispatch_capture_event_loop(struct client_state *state)
{
    // TODO: Assert ffmpeg installed
    //          TODO: Probably use libav manually and don't launch ffmpeg
    // XXX: - Needs better asssert? Intent: make sure selection is complete and valid
    //      Most of this function is probably temporary anyways
    assert(
        state->selection.selection_state == SELECTION_COMPLETE
     || state->selection.selection_state == SELECTION_REBASING
     && state->selection.bl.box.x1
     && state->selection.bl.box.y1
    );

    // XXX: Put all of this more nicely somewhere else ?
    state->capture.frame_width_px = state->selection.bl.box.x1 - state->selection.bl.box.x0;
    state->capture.frame_height_px = state->selection.bl.box.y1 - state->selection.bl.box.y0;
    state->capture.frame_x_px = state->selection.bl.box.x0;
    state->capture.frame_y_px = state->selection.bl.box.y0;

    // XXX: Double-check whether appropriate char-array sizes
    //      Also maybe clean up and/or optimize some of this filename stuff
    char ffmpeg_command[256];
    char time_now_str[64];
    time_t time_now = time(NULL);
    struct tm *tm_now = localtime(&time_now);
    strftime(time_now_str, sizeof(time_now_str), "%Y%m%d-%H%M%S", tm_now);
    snprintf(ffmpeg_command, 256,
        // XXX: Using -v quiet to suppress output and broken newline at end.
        //          TODO: Find better solution that still gives some logging
        "ffmpeg -v quiet -f rawvideo -video_size %dx%d -pix_fmt %s -i -"
            " test-capture_%s.mp4",
        state->capture.frame_width_px,
        state->capture.frame_height_px,
        wl_shm_format_to_ffmpeg_cli_str(state->capture.shm_format),
        time_now_str
    );
    fprintf(stderr, "FFMPEG COMMAND: `%s`\n", ffmpeg_command);

    state->capture.capturing = true;
    state->capture.ffmpeg = popen(ffmpeg_command, "w");
    state->capture.ffmpeg_fd = fileno(state->capture.ffmpeg);


    // Get initial frame. Subsequent capture requests happen within frame::ready
    //     Similar to the wl_surface callback event loop
    ext_image_copy_capture_frame_v1_attach_buffer(
        state->capture.frame,
        state->capture.buffer.buffer
    );
    ext_image_copy_capture_frame_v1_damage_buffer(
        state->capture.frame,
        0,
        0,
        state->capture.source_width_px,
        state->capture.source_height_px
    );
    ext_image_copy_capture_frame_v1_capture(state->capture.frame);

    return true;
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
    fprintf(stderr, "Finished: init_image_copy_capture_frame()\n");

    if (!init_image_copy_capture_frame(&state)) {
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Finished: init_image_copy_capture_frame()\n");
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

    // TEST:
    // TODO: Quit out of loop by keybind and some new status/running/etc state member
    //       And probably not two loops...
    while (wl_display_dispatch(state.globals.display)) {
        if (state.selection.selection_state == SELECTION_COMPLETE) {
            dispatch_capture_event_loop(&state);
            break;
        }
    }
    while (wl_display_dispatch(state.globals.display)) {
        if (state.selection.selection_state != SELECTION_COMPLETE
            && state.selection.selection_state != SELECTION_REBASING
        ) {
            break;
        }
    }

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

