#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <sys/epoll.h>

#include <basu/sd-bus.h>

#include "portals.h"
#include "print.h"


static struct {
    sd_bus *bus;
    int fd;
} m_dbus = { .fd = -1 };


static inline void
_log_sd_bus_ret_error(
    int ret_,
    const char *custom_message
) {
    eprintf("SD-Bus Error: %s\n"
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

void
scran_portal_notify_file_saved(const char *saved_file_path)
{
    if (m_dbus.bus == NULL) {
        DEBUG("Notification not sent (D-Bus not initialized).\n");
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
          3,
            "title", "s", "Scran: saved file.",
            "body",  "s", saved_file_path,
            "display-hint", "as", 1, "show-as-new"
    );

    if (ret < 0) {
        _log_sd_bus_ret_error(
            ret, "FAILED: org.freedesktop.portal.Notification.AddNotification"
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

    // NOTE: Until sd_bus_process() returns 0, there might still be more work
    // left to do. Since we're single-threaded (outside of libav* internals),
    // we only call this once, on the off-chance that a loop would block e.g.
    // a video capture frame.
    //     Might need change if we start using D-Bus more heavily, e.g. ScreenCast.
    //     TODO: Verify that sd_bus_get_timeout() handles this appropriately.
    int ret;
    ret = sd_bus_process(m_dbus.bus, NULL);
    if (0 > ret) {
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

bool
scran_portal_init(int epoll_fd, int *timeout_ms)
{
    if (0 > sd_bus_default_user(&m_dbus.bus)) {
        eprintf("Failed to open D-Bus connection.\n");
        goto fail_before_open;
    }
    eprintf("D-Bus connection opened.\n");
    assert(m_dbus.bus != NULL);

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

