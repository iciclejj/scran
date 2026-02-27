#include <wayland-client.h>

#include "state.h"
#include "event-handlers.h"


static void
handle_surface_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct scran_output_surface_buffer *st_surface_buffer = data;

    st_surface_buffer->busy = false;
}

struct wl_buffer_listener surface_buffer_listener = {
    .release = handle_surface_buffer_release
};



static void
handle_capture_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct scran_capture_buffer *st_capture_buffer = data;

    // Don't need to do anything at the moment...
}

struct wl_buffer_listener capture_buffer_listener = {
    .release = handle_capture_buffer_release
};

