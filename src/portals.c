#include <assert.h>

#include <basu/sd-bus.h>

#include "portals.h"
#include "print.h"

static struct {
    sd_bus *bus;
} m_dbus = { };


static inline void
_log_dbus_error(
    int ret_,
    sd_bus_error error
) {
    eprintf("FAILED: org.freedesktop.portal.Notification.AddNotification\n"
            "        %d: %s\n"
            "        %s: %s\n",
            ret_, strerror(-ret_),
            error.name ? error.name : "-", error.message ? error.message : "-"
            );
}


bool
scran_portal_init()
{
    if (0 > sd_bus_default_user(&m_dbus.bus)) {
        eprintf("Failed to open D-Bus connection.\n");
        m_dbus.bus = NULL;
        return false;
    }

    eprintf("D-Bus connection opened.\n");
    return true;
}

void
scran_portal_destroy()
{
    sd_bus_flush_close_unref(m_dbus.bus);
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
    sd_bus_message *reply;
    sd_bus_error error = SD_BUS_ERROR_NULL;

    static const char default_notification_id[] = "scran-default-id";

    ret = sd_bus_call_method(
        m_dbus.bus,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Notification", "AddNotification",
        &error, &reply,
        "sa{sv}",
          default_notification_id,
          3,
            "title", "s", "Scran: saved file.",
            "body",  "s", saved_file_path,
            "display-hint", "as", 1, "show-as-new"
    );

    if (ret < 0) {
        _log_dbus_error(ret, error);
        goto cleanup;
    }

cleanup:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return;
}

