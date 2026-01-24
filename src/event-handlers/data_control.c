#include <unistd.h>
#include <assert.h>
#include <string.h>

#include "ext-data-control-v1.h"

#include "event-handlers.h"
#include "print.h"

// void 
// handle_data_control_device_selection(
//     void *data,
//     struct ext_data_control_device_v1 *device,
//     struct ext_data_control_offer_v1 *offer
// ) {
//     // TODO: Do we need this if we only care about sending offers, and not
//     // receiving? Destruction of our own sources/offers can happen through
//     // source::cancelled.
//     struct client_state_seat_datacontrol *st_datacontrol = data;
// }
//
// struct ext_data_control_device_v1_listener data_control_device_listener = {
//     .selection = handle_data_control_device_selection,
// };

void
handle_data_control_source_send(
    void *data,
    struct ext_data_control_source_v1 *source,
    const char *mime_type,
    int32_t fd
) {
    struct client_state_seat_datacontrol *st_datacontrol = data;
    const BLArrayCore *const bl_array = &st_datacontrol->data_to_send;


    const char *const our_mime_type = st_datacontrol->data_to_send_mime_type;
    DEBUG("datacontrol_source::send(): Received mimetype: %s\n", our_mime_type);

    // TODO: Maybe custom strcmp ?
    const bool is_mime_type_supported = !strcmp(mime_type, our_mime_type);
    if (!is_mime_type_supported) {
        eprintf("Unsupported mime-type for pasting selection\n");
        return;
    }

    const void *const buf = bl_array_get_data(bl_array);
    const size_t buf_size = bl_array_get_size(bl_array);

    if (buf_size <= write(fd, buf, buf_size)) {
        eprintf("Wrote clipboard selection\n");
    } else {
        eprintf("Failed to write clipboard selection.\n");
    }

    close(fd);
    return;
}

void
handle_data_control_source_cancelled(
    void *data,
    struct ext_data_control_source_v1 *source
) {
    struct client_state_seat_datacontrol *st_datacontrol = data;

    // TODO: Consider destroying the source here. At the moment it should not
    // be necessary (destroyed inside both image_capture::frame() and during
    // exit cleanup)

    st_datacontrol->selection_active = false;
    DEBUG("Clipboard selection de-activated\n");
}

struct ext_data_control_source_v1_listener data_control_source_listener = {
    .send = handle_data_control_source_send,
    .cancelled = handle_data_control_source_cancelled,
};
