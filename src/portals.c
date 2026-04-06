#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>

#include <basu/sd-bus.h>

#include "state.h"
#include "portals.h"
#include "print.h"


extern struct scran g_state;


static struct {
    sd_bus *bus;
    int fd;
} m_dbus = { .fd = -1 };


static inline void
_log_sd_bus_ret_error(
    int ret_,
    const char *custom_message
) {
    eprintf("sd_bus error: %s\n"
            "  %d: %s\n",
            custom_message,
            ret_, strerror(-ret_));
}


static inline void
_log_sd_bus_error(
    const sd_bus_error *error,
    const char *custom_message
) {
    eprintf("%s\n"
            "  %s: %s\n",
            custom_message,
            error->name ? error->name : "-",
            error->message ? error->message : "-"
    );
}

static int
_scran_portal_notify_file_saved_callback(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        _log_sd_bus_error(error, "AddNotification Error\n");
        return 0;
    }

    DEBUG("AddNotification reply without error.\n");
    return 0;
}

static int
_scran_portal_callback_OpenURI_OpenFile(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    // This returns a Request object, but we have no use for it, other than
    // maybe error reporting, so we ignore it to save on complexity.

    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        _log_sd_bus_error(error, "OpenFile Error\n");
        return 0;
    }

    DEBUG("OpenFile reply without error.\n");
    return 0;
}

void
scran_portal_open_file(const char *file_path)
{
    if (m_dbus.bus == NULL) {
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
        m_dbus.bus, NULL,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.OpenURI", "OpenFile",
        &_scran_portal_callback_OpenURI_OpenFile, NULL,
        "sha{sv}",
          parent_window, file_fd, 0
    );

    if (ret < 0) {
        _log_sd_bus_ret_error(
            ret, "Failed to call OpenURI::OpenFile"
        );
    }

    close(file_fd); // sd_bus duplicates this for us on method call
    return;
}


void
scran_portal_notify_file_saved(const char *saved_file_path)
{
    if (m_dbus.bus == NULL) {
        DEBUG("Notification not sent (D-Bus not initialized).\n");
        return;
    }
    if (g_state.options.no_notifications) {
        DEBUG("Notification not sent (options.no_notifications).\n");
        return;
    }

    // TODO: Assert saved_file_path length?

    int ret;

    const char *notification_id = saved_file_path;
    ret = sd_bus_call_method_async(
        m_dbus.bus, NULL,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Notification", "AddNotification",
        &_scran_portal_notify_file_saved_callback, NULL,
        "sa{sv}",
          notification_id,
          5,
            "title", "s", "Scran: saved file.",
            "body",  "s", saved_file_path,
            "display-hint", "as", 1, "show-as-new",
            "default-action", "s", "OpenFile",
            "default-action-target", "s", saved_file_path
    );

    if (ret < 0) {
        _log_sd_bus_ret_error(
            ret, "Failed to call Notification::AddNotification"
        );
    }

    return;
}


static inline int
scran_portal_get_timeout_ms()
{
    uint64_t timeout_abs_usec = UINT64_MAX;
    sd_bus_get_timeout(m_dbus.bus, &timeout_abs_usec);

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

// Should be fired unconditionally after each poll return if we can't guarantee
// that the next poll will happen before the currently (at time of poll return)
// set timeout_ms runs out. See 'man 3 sd_bus_get_{fd/events/timeout}' for more
// details.
// This function is still safe to call if dbus was not successfully initialized,
// and will simply set timeout_ms to -1.
void
scran_portal_update(int epoll_fd, int *timeout_ms)
{
    if (m_dbus.bus == NULL) {
        assert(m_dbus.fd == -1);
        *timeout_ms = -1;
        return;
    }

    int ret;

    // NOTE: Until sd_bus_process() returns 0, there might still be more work
    // left to do. Since we're single-threaded (outside of libav* internals),
    // we only call this once, on the off-chance that a loop would block e.g.
    // a video capture frame.
    //     Might need change if we start using D-Bus more heavily, e.g. ScreenCast.
    //     TODO: Verify that sd_bus_get_timeout() handles this appropriately.
    if (0 > (ret = sd_bus_process(m_dbus.bus, NULL))) {
        _log_sd_bus_ret_error(ret, "sd_bus_process() failed. Stopping SD-Bus connection.");
        goto fail;
    }

    // sd_bus_get_events manpage implies we should always check for a new fd
    int portal_fd = sd_bus_get_fd(m_dbus.bus);
    int _portal_events = sd_bus_get_events(m_dbus.bus);
    struct epoll_event epoll_event = {
        .events = _portal_events,
        .data.fd = portal_fd,
    };
    if (portal_fd == m_dbus.fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, m_dbus.fd, &epoll_event);
    } else {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_dbus.fd, NULL);
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, portal_fd, &epoll_event);
        m_dbus.fd = portal_fd;
    }

    *timeout_ms = scran_portal_get_timeout_ms();
    return;

fail:
    scran_portal_destroy(epoll_fd);
    *timeout_ms = -1;
    return;
}


static int
_scran_portal_callback_Notification_ActionInvoked(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        _log_sd_bus_error(error, "Notification::ActionInvoked");
        return 0;
    }

    int ret;
    static const char parse_error_string[] = "Failed to parse message from Notification::ActionInvoked";

    const char *id;
    const char *action;
    if (0 > (ret = sd_bus_message_read(message, "ss", &id, &action))) {
        _log_sd_bus_ret_error(ret, parse_error_string);
        return 0;
    }

    const char *parameter;
    if (0 > (ret = sd_bus_message_enter_container(message, 'a', "v"))) {
        _log_sd_bus_ret_error(ret, parse_error_string);
        return 0;
    }
    if (0 > (ret = sd_bus_message_read(message, "v", "s", &parameter))) {
        _log_sd_bus_ret_error(ret, parse_error_string);
        return 0;
    }
    // Don't care about the rest of this container...

    const char *filepath = parameter;
    scran_portal_open_file(filepath);

    DEBUG("ActionInvoked reply without error.\n");
    return 0;
}

static int
_scran_portal_callback_Dbus_AddMatch(
    sd_bus_message *message, // Should not be freed.
    void *data,
    sd_bus_error *ret_error // This is for us to return, not to read
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        _log_sd_bus_error(error, "AddMatch Error\n");
        return 0;
    }

    DEBUG("AddMatch reply without error.\n");
    return 0;
}

bool
scran_portal_init(int epoll_fd, int *timeout_ms)
{
    if (0 > sd_bus_default_user(&m_dbus.bus)) {
        eprintf("Failed to open D-Bus connection.\n");
        goto fail_before_open;
    }
    eprintf("D-Bus connection opened.\n");
    assert(m_dbus.bus != NULL);

    int ret = sd_bus_match_signal_async(
        m_dbus.bus, NULL,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Notification", "ActionInvoked",
        _scran_portal_callback_Notification_ActionInvoked,
        _scran_portal_callback_Dbus_AddMatch,
        NULL
    );
    if (ret < 0) {
        _log_sd_bus_ret_error(ret, "Failed to register listener for Notification::ActionInvoked");
    }

    int portal_fd = sd_bus_get_fd(m_dbus.bus);
    int _portal_events = sd_bus_get_events(m_dbus.bus);
    struct epoll_event epoll_event = {
        .events = _portal_events,
        .data.fd = portal_fd,
    };
    if (-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, portal_fd, &epoll_event)) {
        eprintf("Failed to add D-Bus connection to epoll.\n");
        goto fail;
    }
    m_dbus.fd = portal_fd;

    *timeout_ms = scran_portal_get_timeout_ms();
    return true;

fail:
    scran_portal_destroy(epoll_fd);
fail_before_open:
    *timeout_ms = -1;
    return false;
}

void
scran_portal_destroy(int epoll_fd)
{
    if (m_dbus.bus != NULL) {
        sd_bus_flush_close_unref(m_dbus.bus);
        m_dbus.bus = NULL;
        eprintf("D-Bus connection closed.\n");
    }

    if (m_dbus.fd != -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_dbus.fd, NULL);
        m_dbus.fd = -1;
        DEBUG("Deleted portal fd from epoll\n");
    }
}

