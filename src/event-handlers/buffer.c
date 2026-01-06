#include <wayland-client.h>

#include "state.h"

#include "wayland-event-handlers.h"

static void
handle_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct client_state_surface_buffer *st_surface_buffer = data;

    st_surface_buffer->busy = false;
}

struct wl_buffer_listener buffer_listener = {
    .release = handle_buffer_release
};

