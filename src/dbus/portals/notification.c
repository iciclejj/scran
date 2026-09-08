#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>

#include "dbus.h"
#include "state.h"

#include "../dbus_.h"
#include "./notification_.h"


static struct {
    bool notification_actions_enabled;
} m_notification = {0};


static int
reply_AddNotification(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "AddNotification Error\n");
        return 0;
    }

    DEBUG("AddNotification reply without error.\n");
    return 0;
}

void
scran_portal_notify_file_saved(const char *saved_file_path)
{
    if (g_dbus.bus == NULL) {
        DEBUG("Notification not sent (D-Bus not initialized).\n");
        return;
    }

    if (g_state.options.no_notifications) {
        DEBUG("Notification not sent (options.no_notifications).\n");
        return;
    }

    // TODO: Assert saved_file_path length?

    int ret;
    sd_bus_message *message = NULL;

    ret = sd_bus_message_new_method_call(
        g_dbus.bus, &message,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Notification", "AddNotification"
    );
    if (ret < 0) {
        goto finish;
    }

    const char *notification_id = saved_file_path;
    ret = sd_bus_message_append(message, "s", notification_id);
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_message_open_container(message, 'a', "{sv}");
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_message_append(message, "{sv}{sv}{sv}",
        "title",        "s",     "Scran: saved file.",
        "body",         "s",      saved_file_path,
        "display-hint", "as", 1, "show-as-new"
    );
    if (ret < 0) {
        goto finish;
    }

    if (m_notification.notification_actions_enabled) {
        ret = sd_bus_message_append(message, "{sv}{sv}",
            "default-action",        "s", "OpenFile",
            "default-action-target", "s", saved_file_path
        );
        if (ret < 0) {
            goto finish;
        }
    }

    ret = sd_bus_message_close_container(message);
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_call_async(
        g_dbus.bus, NULL, message,
        reply_AddNotification, NULL, 0
    );
    if (ret < 0) {
        goto finish;
    }

finish:
    sd_bus_message_unref(message);

    if (ret < 0) {
        log_sd_bus_ret_error(
            ret, "Failed to call Notification::AddNotification"
        );
    }
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
        log_sd_bus_error(error, "Notification::ActionInvoked");
        return 0;
    }

    int ret;
    static const char parse_error_string[] = "Failed to parse message from Notification::ActionInvoked";

    const char *id;
    const char *action;
    ret = sd_bus_message_read(message, "ss", &id, &action);
    if (ret < 0) {
        goto finish;
    }

    const char *parameter;
    ret = sd_bus_message_enter_container(message, 'a', "v");
    if (ret < 0) {
        goto finish;
    }
    ret = sd_bus_message_read(message, "v", "s", &parameter);
    if (ret < 0) {
        goto finish;
    }
    // Don't care about the rest of this container...

    const char *filepath = parameter;
    scran_portal_open_file(filepath);

    DEBUG("ActionInvoked reply without error.\n");

finish:
    if (ret < 0) {
        log_sd_bus_ret_error(ret, parse_error_string);
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

    DEBUG("AddMatch for Notification::ActionInvoked succeeded.\n");

    m_notification.notification_actions_enabled = true;
    return 0;
}

bool
scran_portal_notification_init()
{
    int ret = sd_bus_match_signal_async(
        g_dbus.bus, NULL,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Notification", "ActionInvoked",
        signal_handler_ActionInvoked,
        reply_AddMatch_ActionInvoked,
        NULL
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to register listener for Notification::ActionInvoked");
        return false;
    }

    return true;
}
