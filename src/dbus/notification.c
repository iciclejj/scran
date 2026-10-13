#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "dbus.h"
#include "state.h"

#include "./dbus_.h"
#include "./notification_.h"


#define OPENFILE_NOTIFICATIONS_LENGTH 8
// 'The default action (usually invoked by clicking the notification)
//  should have a key named "default".'
//   - Basic Design section of fd.o Notification spec
#define NOTIFICATION_ACTION_OPENFILE "default"
#define DBUS_NAME_SIZE_MAX 256 // D-Bus names are at most 255 bytes.

struct openfile_context {
    // Assigned by the notification server. 0 if unused or awaiting the Notify reply.
    uint32_t notification_id;
    char file_path[SCRAN_OUTPUT_FILEPATH_SIZE_MAX];
};

typedef unsigned int openfile_notification_count_t;

static struct {
    // Unique bus name of the server that assigned the stored notification IDs.
    char openfile_notification_server_name[DBUS_NAME_SIZE_MAX];
    // Ring buffer
    struct openfile_context openfile_contexts[OPENFILE_NOTIFICATIONS_LENGTH];
    openfile_notification_count_t openfile_notification_count;
    bool notification_actions_enabled;
} m_notification = {0};

static inline struct openfile_context *
openfile_context_for_sequence(openfile_notification_count_t sequence) {
    _Static_assert(
        (OPENFILE_NOTIFICATIONS_LENGTH & (OPENFILE_NOTIFICATIONS_LENGTH - 1)) == 0,
        "Must be a power of 2 for the ring-buffer to wrap around correctly."
    );
    return &m_notification.openfile_contexts[sequence % OPENFILE_NOTIFICATIONS_LENGTH];
}

static inline struct openfile_context *
openfile_context_stored(openfile_notification_count_t sequence) {
    if (m_notification.openfile_notification_count - sequence > OPENFILE_NOTIFICATIONS_LENGTH) {
        return NULL; // No longer stored
    }
    return openfile_context_for_sequence(sequence);
}

static inline struct openfile_context *
openfile_context_stored_by_id(uint32_t id) {
    // Servers never assign 0. We use it as a sentinel.
    if (id == 0) {
        return NULL;
    }

    struct openfile_context *openfile_ctx = NULL;

    for (int i = 0; i < OPENFILE_NOTIFICATIONS_LENGTH; ++i) {
        if (m_notification.openfile_contexts[i].notification_id == id) {
            openfile_ctx = &m_notification.openfile_contexts[i];
            break;
        }
    }

    return openfile_ctx;
}

static inline bool
openfile_contexts_update_server_name(const char *server_name) {
    size_t server_name_size = server_name ? strlen(server_name) + 1 : 0;
    if (server_name_size == 0 || server_name_size > sizeof(m_notification.openfile_notification_server_name)) {
        return false;
    }
    if (strcmp(server_name, m_notification.openfile_notification_server_name)) {
        // Reset/invalidate all contexts, since their IDs belonged to the
        // previous server.
        for (int i = 0; i < OPENFILE_NOTIFICATIONS_LENGTH; ++i) {
            m_notification.openfile_contexts[i].notification_id = 0;
        }
        memcpy(m_notification.openfile_notification_server_name, server_name, server_name_size);
    }
    return true;
}

static inline bool
openfile_contexts_verify_server_name(const char *server_name) {
    return server_name && !strcmp(server_name, m_notification.openfile_notification_server_name);
}


static int
reply_Notify(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "Notify Error\n");
        return 0;
    }

    DEBUG("Notify reply without error.\n");
    return 0;
}

static void scran_close_notification(const char *server_name, uint32_t id);

static int
openfile_reply_Notify(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "Notify Error\n");
        return 0;
    }

    uint32_t id;
    int ret = sd_bus_message_read(message, "u", &id);
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to parse reply from Notifications::Notify");
        return 0;
    }

    const char *server_name = sd_bus_message_get_sender(message);

    if (!openfile_contexts_update_server_name(server_name)) {
        DEBUG("Notify reply with invalid sender.\n");
        return 0;
    }

    openfile_notification_count_t sequence = (uintptr_t)data;
    struct openfile_context *openfile_ctx = openfile_context_stored(sequence);
    if (!openfile_ctx) {
        DEBUG("Notify reply for an overwritten openfile notification.\n");
        scran_close_notification(server_name, id);
        return 0;
    }
    openfile_ctx->notification_id = id;

    DEBUG("Notify reply without error.\n");
    return 0;
}

struct scran_notification_data {
    const char *title;
    const char *body;
    const char *openfile_path;
};

static void
scran_close_notification(const char *server_name, uint32_t id)
{
    if (id == 0) {
        return;
    }

    int ret = sd_bus_call_method_async(
        g_dbus.bus, NULL,
        server_name, "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "CloseNotification",
        NULL, NULL,
        "u", id
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to call Notifications::CloseNotification");
    }
}

static void
scran_send_notification(const struct scran_notification_data *data)
{
    if (g_dbus.bus == NULL) {
        DEBUG("Notification not sent (D-Bus not initialized).\n");
        return;
    }

    if (g_state.options.no_notifications) {
        DEBUG("Notification not sent (options.no_notifications).\n");
        return;
    }

    int ret;
    sd_bus_message *message = NULL;
    const char *error_message = "Failed to call Notifications::Notify";

    ret = sd_bus_message_new_method_call(
        g_dbus.bus, &message,
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify"
    );
    if (ret < 0) {
        goto finish;
    }
    ret = sd_bus_message_append(message, "susss",
        "Scran",     // app_name
        0,           // replaces_id
        "",          // app_icon
        data->title, // summary
        data->body   // body
    );
    if (ret < 0) {
        goto finish;
    }

    bool want_openfile_action = data->openfile_path && m_notification.notification_actions_enabled;
    if (want_openfile_action) {
        ret = sd_bus_message_append(message, "as", 2, NOTIFICATION_ACTION_OPENFILE, "Open");
    } else {
        ret = sd_bus_message_append(message, "as", 0);
    }
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_message_append(message, "a{sv}i",
        0, // hints
        -1 // expire_timeout (-1 lets server decide)
    );
    if (ret < 0) {
        goto finish;
    }

    if (want_openfile_action) {
        openfile_notification_count_t sequence     = m_notification.openfile_notification_count++;
        struct openfile_context      *openfile_ctx = openfile_context_for_sequence(sequence);
        size_t                        path_size    = strlen(data->openfile_path) + 1;

        // We'll assume that ordering this before the Notify call is enough to
        // ensure our old notification is removed before presenting the new one.
        // If this turns out too fragile, we'll need to either Notify on a
        // CloseNotification callback or at least keep a buffer slightly beyond
        // our intended max active notifications.
        scran_close_notification(m_notification.openfile_notification_server_name, openfile_ctx->notification_id);

        openfile_ctx->notification_id = 0;
        assert(path_size <= sizeof(openfile_ctx->file_path));
        memcpy(openfile_ctx->file_path, data->openfile_path, path_size);

        ret = sd_bus_call_async(
            g_dbus.bus, NULL, message,
            openfile_reply_Notify, (void *)(uintptr_t)sequence, 0
        );
    } else {
        ret = sd_bus_call_async(
            g_dbus.bus, NULL, message,
            reply_Notify, NULL, 0
        );
    }
    if (ret < 0) {
        goto finish;
    }

finish:
    sd_bus_message_unref(message);

    if (ret < 0) {
        log_sd_bus_ret_error(ret, error_message);
    }
}

void
scran_notify_file_saved(
    const char *saved_file_path,
    bool incomplete
) {
    scran_send_notification(
        &(struct scran_notification_data){
            .title =
                incomplete
                ? SCRAN_NOTIFICATION_PREFIX "saved file (may be incomplete)"
                : SCRAN_NOTIFICATION_PREFIX "saved file",
            .body = saved_file_path,
            .openfile_path = saved_file_path,
        }
    );
}

void
scran_notify_error_(const char *title)
{
    scran_send_notification(
        &(struct scran_notification_data){
            .title = title,
            .body = "See terminal/stderr logs for details."
        }
    );
}

static int
reply_OpenURI_OpenFile(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    // This returns a Request object, but we have no use for it, other than
    // maybe error reporting, so we ignore it to save on complexity.

    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "OpenFile Error\n");
        return 0;
    }

    DEBUG("OpenFile reply without error.\n");
    return 0;
}

static void
scran_portal_open_file(const char *file_path)
{
    if (g_dbus.bus == NULL) {
        DEBUG("File not opened (D-Bus not initialized).\n");
        return;
    }

    int ret;

    static const char parent_window[] = "";
    int file_fd = open(file_path, O_RDONLY | O_CLOEXEC);

    if (file_fd < 0) {
        eprintf("Failed to open file descriptor. (%d: %s)\n", file_fd, strerror(errno));
        return;
    }

    ret = sd_bus_call_method_async(
        g_dbus.bus, NULL,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.OpenURI", "OpenFile",
        &reply_OpenURI_OpenFile, NULL,
        "sha{sv}",
          parent_window, file_fd, 0
    );

    if (ret < 0) {
        log_sd_bus_ret_error(
            ret, "Failed to call OpenURI::OpenFile"
        );
    }

    close(file_fd); // sd_bus duplicates this for us on method call
}

static int
signal_handler_ActionInvoked(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "Notifications::ActionInvoked");
        return 0;
    }

    int ret;
    static const char error_message[] = "Notifications::ActionInvoked";

    uint32_t id;
    const char *action;
    ret = sd_bus_message_read(message, "us", &id, &action);
    if (ret < 0) {
        goto finish;
    }

    if (!openfile_contexts_verify_server_name(sd_bus_message_get_sender(message))) {
        DEBUG("ActionInvoked from a server that did not assign the stored notification IDs.\n");
        return 0;
    }

    const struct openfile_context *openfile_ctx = openfile_context_stored_by_id(id);
    if (openfile_ctx == NULL) {
        // Some notification servers broadcast ActionInvoked to all clients.
        DEBUG("ActionInvoked got foreign, overwritten or invalid notification ID.\n");
        return 0;
    }
    if (!strcmp(action, NOTIFICATION_ACTION_OPENFILE)) {
        scran_portal_open_file(openfile_ctx->file_path);
    }

    DEBUG("ActionInvoked reply without error.\n");

finish:
    if (ret < 0) {
        log_sd_bus_ret_error(ret, error_message);
    }

    return 0;
}

static int
reply_AddMatch_ActionInvoked(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "AddMatch Error\n");
        return 0;
    }

    DEBUG("AddMatch for Notifications::ActionInvoked reply without error.\n");

    m_notification.notification_actions_enabled = true;
    return 0;
}

bool
scran_notification_init()
{
    int ret = sd_bus_match_signal_async(
        g_dbus.bus, NULL,
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "ActionInvoked",
        signal_handler_ActionInvoked,
        reply_AddMatch_ActionInvoked,
        NULL
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to register listener for Notifications::ActionInvoked");
        return false;
    }

    return true;
}
