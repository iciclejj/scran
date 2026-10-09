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

struct scran_notification_data {
    const char *id;
    const char *title;
    const char *body;
    const char *default_action;
    const char *default_action_target;
};

static void
scran_portal_send_notification(const struct scran_notification_data *data)
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

    ret = sd_bus_message_new_method_call(
        g_dbus.bus, &message,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Notification", "AddNotification"
    );
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_message_append(message, "s", data->id);
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_message_open_container(message, 'a', "{sv}");
    if (ret < 0) {
        goto finish;
    }

    ret = sd_bus_message_append(message, "{sv}{sv}{sv}",
        "title",        "s",      data->title,
        "body",         "s",      data->body,
        "display-hint", "as", 1, "show-as-new"
    );
    if (ret < 0) {
        goto finish;
    }

    if (data->default_action && m_notification.notification_actions_enabled) {
        ret = sd_bus_message_append(message, "{sv}{sv}",
            "default-action",        "s", data->default_action,
            "default-action-target", "s", data->default_action_target
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

void
scran_portal_notify_file_saved(
    const char *saved_file_path,
    bool incomplete
) {
    // TODO: Assert saved_file_path length?
    scran_portal_send_notification(
        &(struct scran_notification_data){
            .id = saved_file_path,
            .title =
                incomplete
                ? SCRAN_NOTIFICATION_PREFIX "saved file (may be incomplete)"
                : SCRAN_NOTIFICATION_PREFIX "saved file",
            .body = saved_file_path,
            .default_action = "OpenFile",
            .default_action_target = saved_file_path,
        }
    );
}

void
scran_portal_notify_error_(const char *title)
{
    scran_portal_send_notification(
        &(struct scran_notification_data){
            .id = "scran-error",
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

    if (!strcmp(action, "OpenFile")) {
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
    }

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
