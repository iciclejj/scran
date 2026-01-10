#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>

#include "state.h"
#include "wayland-event-handlers.h"
#include "init.h"

// TODO: Clean this up a bit.
bool
init_output_surface_shm_buffers(
    // TODO: Either switch this back to just state, or do this narrowing everywhere
    struct client_state_output_surface *st_surface,
    struct wl_shm *wl_shm_global
) {
    // TODO: Is this more efficient to create in handle_global and/or layer_surface ack_configure?
    int shm_fd = shm_open_anon();
    // TODO: Account for scale/transform
    uint32_t width_bytes = SURFACE_PIXEL_STRIDE * st_surface->width_px;

    st_surface->buf_size = width_bytes * st_surface->height_px;
    st_surface->shm_pool_size = SURFACE_BUF_COUNT * st_surface->buf_size;

    // XXX: MEMORY ALLOC/FREE HERE
    if (-1 == ftruncate(shm_fd, st_surface->shm_pool_size)) {
        fprintf(stderr, "Failed to resize shm file to %d\n", st_surface->shm_pool_size);
        close(shm_fd);
        return false;
    }
    fprintf(stderr, "Resized shm file to %d\n", st_surface->shm_pool_size);

    st_surface->shm_pool = wl_shm_create_pool(
        wl_shm_global,
        shm_fd,
        st_surface->shm_pool_size
    );

    // TODO: Collect all mmaps into one
    // XXX: MEMORY ALLOC/FREE HERE
    st_surface->double_buffer[0].data = mmap(
        NULL, st_surface->shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0
    );
    st_surface->double_buffer[1].data =
        st_surface->double_buffer[0].data + st_surface->buf_size;

    for (int i = 0; i < SURFACE_BUF_COUNT; i++) {
        fprintf(stderr, "Creating buffer %d\n", i);
        assert(i * st_surface->buf_size <= st_surface->shm_pool_size);

        int _pool_offset = i * st_surface->buf_size;

        st_surface->double_buffer[i].buffer = wl_shm_pool_create_buffer(
            st_surface->shm_pool,
            _pool_offset,
            st_surface->width_px,
            st_surface->height_px,
            width_bytes,
            SURFACE_SHM_FORMAT
        );

        wl_buffer_add_listener(
            st_surface->double_buffer[i].buffer,
            &buffer_listener,
            &st_surface->double_buffer[i]
        );
    }

    // XXX: MEMORY ALLOC/FREE HERE
    close(shm_fd);
    // TODO: Defer this until end of program execution?
    //           Decide for this and other destroys
    //           Else don't save shm_pool or shm_pool_size anywhere
    wl_shm_pool_destroy(st_surface->shm_pool);

    if (st_surface->double_buffer[0].data == NULL) {
        return false;
    }

    // TODO: Should this be done here?
    wl_surface_attach(st_surface->surface, st_surface->double_buffer[0].buffer, 0, 0);

    return true;
}

void
destroy_output_surface_shm_buffers(struct client_state_output_surface *st_surface)
{
    for (int i = 0; i < SURFACE_BUF_COUNT; ++i) {
        // XXX: MEMORY ALLOC/FREE HERE
        wl_buffer_destroy(st_surface->double_buffer[i].buffer);
    }
}

bool
init_output_surface(
    struct client_state_output *st_output,
    struct client_state_globals *st_globals
) {
    // Must add role to surface and ack its configure event before adding a buffer.
    st_output->surface.surface = wl_compositor_create_surface(st_globals->compositor);
    st_output->surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        st_globals->layer_shell,
        st_output->surface.surface,
        st_output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "test_client_namespace" // TODO: Figure out a namespace name?
    );

    zwlr_layer_surface_v1_set_exclusive_zone(st_output->surface.layer_surface, -1);
    // Need to set at least anchors before configure event,
    // so that the compositor knows what width/height to give us.
    zwlr_layer_surface_v1_set_anchor(
        st_output->surface.layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
    );
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        st_output->surface.layer_surface,
        // TODO: Figure out whether this should rather be set to "exclusive"
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
    );

    zwlr_layer_surface_v1_add_listener(st_output->surface.layer_surface, &layer_surface_listener, &st_output->surface);
    // Initial bufferless commit to trigger configure event
    wl_surface_commit(st_output->surface.surface);

    // XXX: This also triggers initial wl_seat listener events at the moment,
    //      due to nested add_listener functions
    //          seat_listener --> pointer_listener, keyboard_listener ...
    if (-1 == wl_display_roundtrip(st_globals->display)) {
        fprintf(stderr, "Display roundtrip after adding zwlr_layer_surface_v1 listener failed.\n");
        return false;
    }

    return true;
}

