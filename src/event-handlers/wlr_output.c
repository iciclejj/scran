/*
 * This file has event handlers for:
 *     wlr_output_manager
 *     wlr_output_head
 *     cosmic_output_head
 *
 * A little messy compared to the other event handler files, but these are so
 * interconnected that separating them would just make it even more convoluted.
 *
 * The cosmic_output_head is an extension of wlr_output_head, and shares its
 * ::done event.
 *
 */

#include <wayland-client.h>
#include "wlr-output-management-unstable-v1.h"
#include "cosmic-output-management-unstable-v1.h"

#include "state.h"
#include "state-util.h"
#include "print.h"


extern struct scran g_state;

struct _pending_head{
    struct scran_output *st_output;

    wl_fixed_t fractional_scale_wlr;
    int32_t fractional_scale_cosmic_1000;

    struct zcosmic_output_head_v1 *cosmic_head;
};

static int _n_pending_heads = 0;
static struct _pending_head _pending_heads[MAX_OUTPUTS] = { };


// See comment in our `handle_fractional_scale_preferred_scale` for more info,
static inline void
handle_wlr_output_head_scale(
    void *data,
    struct zwlr_output_head_v1 *output_head,
    wl_fixed_t scale
) {
    struct _pending_head *pending_head = data;

    pending_head->fractional_scale_wlr = scale;
    DEBUG("handle_wlr_output_head_scale(): %f\n", _get_normalized_scaler(scale, 256));
}

void handle_wlr_output_head_name(
    void *data,
    struct zwlr_output_head_v1 *wlr_output_head,
    const char *name
) {
    struct _pending_head *pending_head = data;

    DEBUG("handle_wlr_output_head_name\n");

    // Match wlr_output_head with wl_output so we can set data pointers appropriately.
    for (int i = 0; i < g_state.n_outputs; ++i) {
        struct scran_output *st_output = &g_state.outputs[i];

        if (0 == strncmp(name, st_output->name, sizeof(st_output->name))) {
            pending_head->st_output = st_output;
            DEBUG("Matched wlr/cosmic_output_head with wl_output.\n");
            return;
        }
    }
}

void handle_wlr_output_head_finished(
    void *data,
    struct zwlr_output_head_v1 *head
) {
    struct _pending_head *pending_head = data;

    zwlr_output_head_v1_release(head);
    zcosmic_output_head_v1_release(pending_head->cosmic_head);
}

void handle_wlr_output_head_description( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, const char *description)           { /* No-op. */ }
void handle_wlr_output_head_physical_size( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, int32_t width, int32_t height)   { /* No-op. */ }
void handle_wlr_output_head_mode( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, struct zwlr_output_mode_v1 *mode)         { /* No-op. */ }
void handle_wlr_output_head_enabled( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, int32_t enabled)                       { /* No-op. */ }
void handle_wlr_output_head_current_mode( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, struct zwlr_output_mode_v1 *mode) { /* No-op. */ }
void handle_wlr_output_head_position( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, int32_t x, int32_t y)                 { /* No-op. */ }
void handle_wlr_output_head_transform( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, int32_t transform)                   { /* No-op. */ }
void handle_wlr_output_head_make( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, const char *make)                         { /* No-op. */ }
void handle_wlr_output_head_model( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, const char *model)                       { /* No-op. */ }
void handle_wlr_output_head_serial_number( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, const char *serial_number)       { /* No-op. */ }
void handle_wlr_output_head_adaptive_sync( void *data, struct zwlr_output_head_v1 *zwlr_output_head_v1, uint32_t state)                  { /* No-op. */ }

struct zwlr_output_head_v1_listener wlr_output_head_listener = {
    .name = handle_wlr_output_head_name,
    .description = handle_wlr_output_head_description,
    .scale = handle_wlr_output_head_scale,
    .physical_size = handle_wlr_output_head_physical_size,
    .mode = handle_wlr_output_head_mode,
    .enabled = handle_wlr_output_head_enabled,
    .current_mode = handle_wlr_output_head_current_mode,
    .position = handle_wlr_output_head_position,
    .transform = handle_wlr_output_head_transform,
    .finished = handle_wlr_output_head_finished,
    .make = handle_wlr_output_head_make,
    .model = handle_wlr_output_head_model,
    .serial_number = handle_wlr_output_head_serial_number,
    .adaptive_sync = handle_wlr_output_head_adaptive_sync,
};


// See comment in our `handle_fractional_scale_preferred_scale` for more info,
void handle_cosmic_output_head_scale_1000(
    void *data,
    struct zcosmic_output_head_v1 *zcosmic_output_head_v1,
    int32_t scale_1000
) {
    struct _pending_head *pending_head = data;

    pending_head->fractional_scale_cosmic_1000 = scale_1000;
    DEBUG("handle_cosmic_output_head_scale_1000(): %f\n", _get_normalized_scaler(scale_1000, 1000));
}

void handle_cosmic_output_head_mirroring( void *data, struct zcosmic_output_head_v1 *zcosmic_output_head_v1, const char *name) { /* No-op. */ }
void handle_cosmic_output_head_adaptive_sync_available( void *data, struct zcosmic_output_head_v1 *zcosmic_output_head_v1, uint32_t available) { /* No-op. */ }
void handle_cosmic_output_head_adaptive_sync_ext( void *data, struct zcosmic_output_head_v1 *zcosmic_output_head_v1, uint32_t state) { /* No-op. */ }
void handle_cosmic_output_head_xwayland_primary( void *data, struct zcosmic_output_head_v1 *zcosmic_output_head_v1, uint32_t state) { /* No-op. */ }

struct zcosmic_output_head_v1_listener cosmic_output_head_listener = {
    .scale_1000 = handle_cosmic_output_head_scale_1000,
    .mirroring = handle_cosmic_output_head_mirroring,
    .adaptive_sync_available = handle_cosmic_output_head_adaptive_sync_available,
    .adaptive_sync_ext = handle_cosmic_output_head_adaptive_sync_ext,
    .xwayland_primary = handle_cosmic_output_head_xwayland_primary,
};


void handle_wlr_output_manager_head(
    void *data,
    struct zwlr_output_manager_v1 *zwlr_output_manager_v1,
    struct zwlr_output_head_v1 *head
) {
    struct scran *state = data;

    if (state->globals.cosmic_output_manager == NULL) {
        DEBUG("cosmic_output_manager protocol not found; won't use"
              " wlr_output_manager or zcosmic_output_manager for scaling.\n");
        return;
    }

    // XXX TODO: We need to remove/ignore heads that are not ::enabled, and
    // free them up for new heads, since heads may outnumber wl_outputs,
    // as wl_outputs are only enabled/turned-on monitors. MAX_OUTPUTS as the
    // size of the struct list should also be changed accordingly. Need to make
    // some simple allocator for the heads.
    if (_n_pending_heads >= MAX_OUTPUTS) {
        eprintf("WARNING: Ran out of space for 'wlr_output_head's. Will use"
                " fallback values for scaling the remaining outputs.\n");
        return;
    }

    struct _pending_head *pending_head = &_pending_heads[_n_pending_heads];

    struct zcosmic_output_head_v1 *cosmic_head = zcosmic_output_manager_v1_get_head(
        state->globals.cosmic_output_manager, head
    );
    zcosmic_output_head_v1_add_listener(
        cosmic_head,
        &cosmic_output_head_listener,
        pending_head
    );
    // Store so we can release it later.
    // We don't need to store wlr_output_head, since it will come in the
    // ::finished event.
    pending_head->cosmic_head = cosmic_head;

    zwlr_output_head_v1_add_listener(
        head,
        &wlr_output_head_listener,
        pending_head
    );

    ++_n_pending_heads;
}

void handle_wlr_output_manager_done(
    void *data,
    struct zwlr_output_manager_v1 *zwlr_output_manager_v1,
    uint32_t serial
) {
    struct scran *state = data;

    for (int i = 0; i < _n_pending_heads; ++i) {
        struct _pending_head *pending_head = &_pending_heads[i];
        struct scran_output *st_output = pending_head->st_output;

        if (st_output == NULL) {
            continue; // Didn't match output through name yet
        }

        // TODO: Rework update_selection_surface_scale_and_size so that we
        // don't need to store them in the main state structs?
        st_output->fractional_scale_cosmic_1000 = pending_head->fractional_scale_cosmic_1000;
        st_output->fractional_scale_wlr = pending_head->fractional_scale_wlr;

        update_selection_surface_scale_and_size(st_output);
        update_selection_surface_viewport(st_output);
    }
}

void handle_wlr_output_manager_finished(
    void *data,
    struct zwlr_output_manager_v1 *manager
) {
    // struct scran *state = data;
    zwlr_output_manager_v1_destroy(manager);
}

struct zwlr_output_manager_v1_listener wlr_output_manager_listener = {
    .head = handle_wlr_output_manager_head,
    .done = handle_wlr_output_manager_done,
    .finished = handle_wlr_output_manager_finished,
};
