#include <wayland-client.h>

#include "state.h"
#include "state-util.h"
#include "freezeframe.h"
#include "event-handlers.h"
#include "print.h"


extern struct scran g_state;


static void
handle_surface_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct scran_output_selectionSurface_buffer *st_buffer = data;

    st_buffer->busy = false;
}

struct wl_buffer_listener selectionSurface_buffer_listener = {
    .release = handle_surface_buffer_release
};



static void
handle_capture_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct scran_capture_buffer *st_capture_buffer = data;
    (void)st_capture_buffer;

    // Don't need to do anything at the moment...
}

struct wl_buffer_listener capture_buffer_listener = {
    .release = handle_capture_buffer_release
};


static void
handle_freezeframe_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    DEBUG("buffer<Freezeframe>::release()\n");

    struct scran_freezeframe_buffer *buffer = data;
    freezeframe_callback callback = buffer->release_callback;

    buffer->release_callback = NULL;
    buffer->busy = false;

    if (callback) {
        struct scran_output *st_output = &g_state.outputs[get_containing_output_array_index(buffer)];

        DEBUG("  calling callback\n");
        callback(st_output);
    }
}

struct wl_buffer_listener freezeframe_buffer_listener = {
    .release = handle_freezeframe_buffer_release
};
