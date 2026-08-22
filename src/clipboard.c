#include "clipboard.h"
#include <assert.h>

#include <blend2d/blend2d.h>

#include "ext-data-control-v1.h"

#include "state.h"
#include "print.h"
#include "event-handlers.h"


static inline bool
copy_data_weak(
    BLArrayCore *dst,
    BLArrayCore *src
) {
    if (bl_array_assign_weak(dst, src) != BL_SUCCESS) {
        eprintf("Error: Failed to copy data for clipboard");
        return false;
    }

    return true;
}

static inline bool
copy_mime_type(
    char dst[static restrict SCRAN_MIME_TYPE_SIZE_MAX],
    const char *restrict src
) {
    if (strlcpy(dst, src, SCRAN_MIME_TYPE_SIZE_MAX)
        >= SCRAN_MIME_TYPE_SIZE_MAX
    ) {
        eprintf("Error: mime-type too long for clipboard. THIS IS A BUG, please open an issue.\n");
        return false;
    }

    return true;
}

static inline bool
copy_filepath(
    char dst[static restrict SCRAN_OUTPUT_FILEPATH_SIZE_MAX],
    const char *src,
    size_t *strlen
) {
    // TODO?: This could be optimized by e.g. storing the path length or storing
    // the output_directory part statically.
    *strlen = strlcpy(dst, src, SCRAN_OUTPUT_FILEPATH_SIZE_MAX);

    if (*strlen >= SCRAN_OUTPUT_FILEPATH_SIZE_MAX) {
        eprintf("Error: file-path too long for clipboard. THIS IS A BUG, please open an issue.\n");
        return false;
    }

    return true;
}

bool
clipboard_update(
    struct scran_seat_datacontrol *datacontrol,
    BLArrayCore *data, // Will be weak-copied. Caller should still _destroy().
    const char *data_mime_type,
    const char *filepath
) {
    DEBUG("Updating clipboard\n");

    struct ext_data_control_source_v1 *data_control_source =
        ext_data_control_manager_v1_create_data_source(
            g_state.globals.data_control_manager
        );

    bl_array_reset(&datacontrol->data_to_send);
    datacontrol->should_offer_data = false;
    datacontrol->should_offer_filepath = false;

    if (data != NULL && data_mime_type != NULL) {
        if (!copy_data_weak(&datacontrol->data_to_send, data)) {
            goto err;
        }
        if (!copy_mime_type(datacontrol->data_to_send_mime_type, data_mime_type)) {
            goto err;
        }

        datacontrol->should_offer_data = true;
        ext_data_control_source_v1_offer(
            data_control_source,
            datacontrol->data_to_send_mime_type
        );
    }

    if (filepath != NULL) {
        if (!copy_filepath(datacontrol->data_to_send_saved_file_path,
                            filepath,
                            &datacontrol->data_to_send_saved_file_path_strlen
        )) {
            goto err;
        }

        datacontrol->should_offer_filepath = true;
        ext_data_control_source_v1_offer(
            data_control_source,
            SCRAN_MIME_TYPE_FILEPATH_URI_LIST
        );

        ext_data_control_source_v1_offer(
            data_control_source,
            SCRAN_MIME_TYPE_FILEPATH_PLAIN
        );
    }

    ext_data_control_source_v1_add_listener(
        data_control_source,
        &data_control_source_listener,
        datacontrol
    );
    ext_data_control_device_v1_set_selection(datacontrol->device, data_control_source);
    // TODO: Relaxed might not end up being enough. Revisit this if we ever do
    // go multithreaded (atomics are not doing anything useful at the moment).
    atomic_fetch_add_explicit(&datacontrol->selection_refcount, 1, memory_order_relaxed);

    return true;

err:
    ext_data_control_source_v1_destroy(data_control_source);

    return false;
}
