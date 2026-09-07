#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>

#include "state.h"
#include "dbus.h"
#include "print.h"

#include "./dbus_.h"
#include "./tray_.h"

struct scran_dbus g_dbus = { .fd = -1 };


static int
get_sd_bus_timeout_ms()
{
    uint64_t timeout_abs_usec = UINT64_MAX;
    sd_bus_get_timeout(g_dbus.bus, &timeout_abs_usec);

    switch (timeout_abs_usec) {
    case 0:
        return 0;
    case UINT64_MAX:
        return -1;
    default:
        break;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    uint64_t now_usec = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;

    if (timeout_abs_usec < now_usec) {
        return 0;
    }

    // Rounding up; see 3 sd_bus_get_timeout
    int rel_ms = (timeout_abs_usec - now_usec + 999) / 1000;

    return rel_ms;
}


static int
OpenURI_OpenFile_callback(
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
        &OpenURI_OpenFile_callback, NULL,
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
Notification_AddNotification_callback(
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

    if (g_dbus.notification_actions_enabled) {
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
        Notification_AddNotification_callback, NULL, 0
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
Notification_ActionInvoked_callback(
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

int
Dbus_AddMatch_callback__generic(
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

    DEBUG("AddMatch reply without error.\n");
    return 0;
}

static int
Dbus_AddMatch_callback__Notification_ActionInvoked(
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

    g_dbus.notification_actions_enabled = true;
    return 0;
}


bool
scran_dbus_init(int epoll_fd, int *timeout_ms)
{
    if (0 > sd_bus_default_user(&g_dbus.bus)) {
        eprintf("Failed to open D-Bus connection.\n");
        goto fail_before_open;
    }
    eprintf("D-Bus connection opened.\n");
    assert(g_dbus.bus != NULL);

    if (!g_state.options.no_notifications) {
        int ret = sd_bus_match_signal_async(
            g_dbus.bus, NULL,
            "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Notification", "ActionInvoked",
            Notification_ActionInvoked_callback,
            Dbus_AddMatch_callback__Notification_ActionInvoked,
            NULL
        );
        if (ret < 0) {
            log_sd_bus_ret_error(ret, "Failed to register listener for Notification::ActionInvoked");
        }
    }

    if (!register_StatusNotifierItem()) {
        eprintf("Warning: Failed to create tray icon\n.");
    }

    int dbus_fd = sd_bus_get_fd(g_dbus.bus);
    int _dbus_events = sd_bus_get_events(g_dbus.bus);
    struct epoll_event epoll_event = {
        .events = _dbus_events,
        .data.fd = dbus_fd,
    };
    if (-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dbus_fd, &epoll_event)) {
        eprintf("Failed to add D-Bus connection to epoll.\n");
        goto fail;
    }
    g_dbus.fd = dbus_fd;

    *timeout_ms = get_sd_bus_timeout_ms();
    return true;

fail:
    scran_dbus_destroy(epoll_fd);
fail_before_open:
    *timeout_ms = -1;
    return false;
}


// Should be fired unconditionally after each poll return if we can't guarantee
// that the next poll will happen before the currently (at time of poll return)
// set timeout_ms runs out. See 'man 3 sd_bus_get_{fd/events/timeout}' for more
// details.
// This function is still safe to call if dbus was not successfully initialized,
// and will simply set timeout_ms to -1.
void
scran_dbus_update(int epoll_fd, int *timeout_ms)
{
    if (g_dbus.bus == NULL) {
        assert(g_dbus.fd == -1);
        *timeout_ms = -1;
        return;
    }

    // NOTE: Until sd_bus_process() returns 0, there might still be more work
    // left to do. Since our main loop is single-threaded, we limit how many
    // calls we allow per call, on the off-chance that a loop would block e.g.
    // a video capture frame.
    int process_ret;
    for (int i = 0; i < 8; ++i) {
        process_ret = sd_bus_process(g_dbus.bus, NULL);

        if (process_ret == 0) {
            break;
        }

        if (process_ret < 0) {
            log_sd_bus_ret_error(process_ret, "sd_bus_process() failed. Stopping SD-Bus connection.");
            goto fail;
        }
    }

    // sd_bus_get_events manpage implies we should always check for a new fd
    int dbus_fd = sd_bus_get_fd(g_dbus.bus);
    int _dbus_events = sd_bus_get_events(g_dbus.bus);
    struct epoll_event epoll_event = {
        .events = _dbus_events,
        .data.fd = dbus_fd,
    };
    if (dbus_fd == g_dbus.fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, g_dbus.fd, &epoll_event);
    } else {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, g_dbus.fd, NULL);
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dbus_fd, &epoll_event);
        g_dbus.fd = dbus_fd;
    }

    if (process_ret > 0) {
        *timeout_ms = 0; // May not have finished all processing
    } else {
        *timeout_ms = get_sd_bus_timeout_ms();
    }

    return;

fail:
    scran_dbus_destroy(epoll_fd);
    *timeout_ms = -1;
}


void
scran_dbus_destroy(int epoll_fd)
{
    scran_dbus_destroy_StatusNotifierItem();

    if (g_dbus.bus != NULL) {
        sd_bus_flush_close_unref(g_dbus.bus);
        g_dbus.bus = NULL;
        eprintf("D-Bus connection closed.\n");
    }

    if (g_dbus.fd != -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, g_dbus.fd, NULL);
        g_dbus.fd = -1;
        DEBUG("Deleted D-Bus fd from epoll\n");
    }
}
