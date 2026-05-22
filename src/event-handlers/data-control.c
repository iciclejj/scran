#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdatomic.h>

#include "ext-data-control-v1.h"

#include "event-handlers.h"
#include "print.h"
#include "clipboard.h"
#include "util/util.h"

// void 
// handle_data_control_device_selection(
//     void *data,
//     struct ext_data_control_device_v1 *device,
//     struct ext_data_control_offer_v1 *offer
// ) {
//     // TODO: Do we need this if we only care about sending offers, and not
//     // receiving? Destruction of our own sources/offers can happen through
//     // source::cancelled.
//     struct scran_seat_datacontrol *st_datacontrol = data;
// }
//
// struct ext_data_control_device_v1_listener data_control_device_listener = {
//     .selection = handle_data_control_device_selection,
// };

void
handle_data_control_source_send(
    void *data_,
    struct ext_data_control_source_v1 *source,
    const char *requested_mime,
    int32_t fd
) {
    struct scran_seat_datacontrol *st_datacontrol = data_;

    DEBUG("datacontrol_source::send(): Received mimetype: %s\n", requested_mime);

    const char *const data_mime = st_datacontrol->data_to_send_mime_type;
    const char *const filepath = st_datacontrol->data_to_send_saved_file_path;
    const size_t filepath_strlen = st_datacontrol->data_to_send_saved_file_path_strlen;

    if (
        st_datacontrol->should_offer_data
        && 0 == strcmp(requested_mime, data_mime)
    ) {
        const BLArrayCore *const bl_array = &st_datacontrol->data_to_send;

        const void *const data = bl_array_get_data(bl_array);
        const size_t data_size = bl_array_get_size(bl_array);
        if (!scran_full_write(fd, data, data_size)) {
            goto failed;
        }
    } else if (
        st_datacontrol->should_offer_filepath
        && 0 == strcmp(requested_mime, SCRAN_MIME_TYPE_FILEPATH_URI_LIST)
    ) {
        static const char prefix[] = "file://";
        static const size_t prefix_strlen = sizeof(prefix) - 1;
        if (!scran_full_write(fd, prefix, prefix_strlen)) {
            goto failed;
        }

        // TODO: Make sure this is an absolute path. Either here or
        // normalize passed path to absolute during option init.
        if (!scran_full_write(fd, filepath, filepath_strlen)) {
            goto failed;
        }

        static const char suffix[] = "\r\n";
        static const size_t suffix_strlen = sizeof(suffix) - 1;
        if (!scran_full_write(fd, suffix, suffix_strlen)) {
            goto failed;
        }
    } else if (
        st_datacontrol->should_offer_filepath
        && 0 == strcmp(requested_mime, SCRAN_MIME_TYPE_FILEPATH_PLAIN)
    ) {
        if (!scran_full_write(fd, filepath, filepath_strlen)) {
            goto failed;
        }
    } else {
        eprintf("Received clipboard request for unknown MIME type.\n");
        goto failed;
    }

    eprintf("Wrote clipboard selection\n");
    close(fd);
    return;

failed:
    eprintf("Error while writing clipboard selection; aborting.\n");
    close(fd);
    return;
}


void
handle_data_control_source_cancelled(
    void *data,
    struct ext_data_control_source_v1 *source
) {
    struct scran_seat_datacontrol *st_datacontrol = data;

    ext_data_control_source_v1_destroy(source);
    DEBUG("clipboard selection destroyed\n");

    atomic_fetch_sub_explicit(&st_datacontrol->selection_refcount, 1, memory_order_relaxed);
    assert(atomic_load(&st_datacontrol->selection_refcount) >= 0);
}


struct ext_data_control_source_v1_listener data_control_source_listener = {
    .send = handle_data_control_source_send,
    .cancelled = handle_data_control_source_cancelled,
};

