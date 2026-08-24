#include <wayland-client.h>

#include "state.h"
#include "event-handlers.h"


static void
handle_scran_wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    struct scran_wl_buffer *buffer = data;
    scran_wl_buffer_release_callback callback = buffer->release_callback;

    buffer->release_callback = NULL;
    buffer->busy = false;

    if (callback) {
        callback(buffer);
    }
}

struct wl_buffer_listener scran_wl_buffer_listener = {
    .release = handle_scran_wl_buffer_release
};
