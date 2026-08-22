#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>

#ifdef SCRAN_LIBSYSTEMD_SD_BUS
  #include <systemd/sd-bus.h>
#else
  #include <basu/sd-bus.h>
#endif

#include "state.h"
#include "dbus.h"
#include "selection.h"
#include "print.h"
#include "util/util.h"


static struct {
    sd_bus *bus;
    int fd;

    bool         StatusNotifierItem_name_registered;         // Name registered with DBus
    bool         StatusNotifierItem_registered_with_watcher; // Item registered with StatusNotifierWatcher
    sd_bus_slot *StatusNotifierItem_slot_vtable;
    sd_bus_slot *StatusNotifierItem_slot_RequestName_callback;
    sd_bus_slot *StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback;
    sd_bus_slot *StatusNotifierWatcher_slot_NameOwnerChanged_match;
} m_dbus = { .fd = -1 };

static inline void
set_StatusNotifierItem_registered_with_watcher(bool registered)
{
    m_dbus.StatusNotifierItem_registered_with_watcher = registered;
    update_focus_released_keymap_text(registered);
}


static inline void
log_sd_bus_ret_error(
    int ret_,
    const char *custom_message
) {
    eprintf("sd_bus error: %s\n"
            "  %d: %s\n",
            custom_message,
            ret_, strerror(-ret_));
}

static inline void
log_sd_bus_error(
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


static inline int
get_sd_bus_timeout_ms()
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
        &Notification_AddNotification_callback, NULL,
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
        log_sd_bus_ret_error(
            ret, "Failed to call Notification::AddNotification"
        );
    }

}

static bool register_StatusNotifierItem_with_watcher(void);

static int
Dbus_NameOwnerChanged_callback__StatusNotifierWatcher(
    sd_bus_message *message,
    void *data,
    sd_bus_error *ret_error
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        const sd_bus_error *error = sd_bus_message_get_error(message);
        log_sd_bus_error(error, "DBus::NameOwnerChanged (for StatusNotifierItem)");
        return 0;
    }

    int ret;
    const char *name;
    const char *old_owner;
    const char *new_owner;

    if (0 > (ret = sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner))) {
        return ret;
    }

    if (old_owner[0] != '\0') { // Previous owner lost ownership
        set_StatusNotifierItem_registered_with_watcher(false);
        m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback = sd_bus_slot_unref(
            m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback
        );
    }

    if (new_owner[0] != '\0') { // New owner exists
        if (m_dbus.StatusNotifierItem_name_registered) {
            register_StatusNotifierItem_with_watcher(); // Try to re-register with new owner
        }
    }

    return 0;
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
    if (0 > (ret = sd_bus_message_read(message, "ss", &id, &action))) {
        log_sd_bus_ret_error(ret, parse_error_string);
        return 0;
    }

    const char *parameter;
    if (0 > (ret = sd_bus_message_enter_container(message, 'a', "v"))) {
        log_sd_bus_ret_error(ret, parse_error_string);
        return 0;
    }
    if (0 > (ret = sd_bus_message_read(message, "v", "s", &parameter))) {
        log_sd_bus_ret_error(ret, parse_error_string);
        return 0;
    }
    // Don't care about the rest of this container...

    const char *filepath = parameter;
    scran_portal_open_file(filepath);

    DEBUG("ActionInvoked reply without error.\n");
    return 0;
}

static int
Dbus_AddMatch_callback(
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

#define STATUS_NOTIFIER_ITEM_NAME_BASE "org.kde.StatusNotifierItem-"
static char m_StatusNotifierItem_name[64] = STATUS_NOTIFIER_ITEM_NAME_BASE;

#define STATUS_NOTIFIER_ITEM_ICON_NAME "camera-photo"

struct StatusNotifierItem_icon_pixmap {
    int  width;
    int  height;
    size_t _data_size;
    const uint8_t *data;
};

struct StatusNotifierItem_data {
    const char    *Category;
    const char    *Id;
    const char    *Title;
    const char    *Status;
    const uint32_t WindowId;
    const int      ItemIsMenu;
    const char    *IconName;

    // For getters
    const struct {
        const char *icon_name;
        const struct StatusNotifierItem_icon_pixmap *icon_pixmaps;
        size_t _icon_pixmaps_len;
        const char *title;
        const char *description; // Supports subset of html
    } ToolTip;
} m_StatusNotifierItem_data = {
    .Category   = "ApplicationStatus",
    .Id         = "scran",
    .Title      = "Scran",
    .Status     = "Active",
    .WindowId   = 0, // TODO: Can we target scran's layer shell?

    // TODO: Menu
    .ItemIsMenu = (int)false,
    // .Menu = "",

    .IconName   = STATUS_NOTIFIER_ITEM_ICON_NAME,
    .ToolTip = {
        // TODO: What does icon_name actually do? Seems unused on swaybar and Waybar.
        .icon_name         = STATUS_NOTIFIER_ITEM_ICON_NAME,
        .icon_pixmaps      = NULL,
        ._icon_pixmaps_len = 0,
        .title             = "Scran",
        .description       = "Grab focus",
    },
};

static inline int
append_StatusNotifierItem_icon_pixmaps(
    sd_bus_message *message,
    const struct StatusNotifierItem_icon_pixmap *pixmaps,
    size_t n_pixmaps
) {
    assert(pixmaps != NULL || n_pixmaps == 0);
    int ret = 0;

    for (size_t i = 0; i < n_pixmaps; ++i) {
        if (0 > (ret = sd_bus_message_open_container(message, SD_BUS_TYPE_STRUCT, "iiay"))) {
            return ret;
        }
        {
            if (0 > (ret = sd_bus_message_append(message, "i", pixmaps[i].width))) {
                return ret;
            }
            if (0 > (ret = sd_bus_message_append(message, "i", pixmaps[i].height))) {
                return ret;
            }
            if (0 > (ret = sd_bus_message_append_array(message, 'y',  pixmaps[i].data, pixmaps[i]._data_size))) {
                return ret;
            }
        }
        if (0 > (ret = sd_bus_message_close_container(message))) {
            return ret;
        }
    }

    return ret;
}

#define STATUS_NOTIFIER_ITEM_TOOL_TIP_TYPE "(sa(iiay)ss)"
static int
StatusNotifierItem_ToolTip_get_property(
    sd_bus *bus,
    const char *path,
    const char *interface,
    const char *property,
    sd_bus_message *reply,
    void *userdata,
    sd_bus_error *error
) {
    int ret;

    if (0 > (ret = sd_bus_message_open_container(reply, SD_BUS_TYPE_STRUCT, "sa(iiay)ss"))) {
        return ret;
    }
    {
        if (0 > (ret = sd_bus_message_append(reply, "s", m_StatusNotifierItem_data.ToolTip.icon_name))) {
            return ret;
        }
        if (0 > (ret = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "(iiay)"))) {
            return ret;
        }
        {
            if (0 > (ret = append_StatusNotifierItem_icon_pixmaps(
                                reply, m_StatusNotifierItem_data.ToolTip.icon_pixmaps,
                                m_StatusNotifierItem_data.ToolTip._icon_pixmaps_len))
            ) {
                return ret;
            }
        }
        if (0 > (ret = sd_bus_message_close_container(reply))) {
            return ret;
        }
        if (0 > (ret = sd_bus_message_append(reply, "s", m_StatusNotifierItem_data.ToolTip.title))) {
            return ret;
        }
        if (0 > (ret = sd_bus_message_append(reply, "s", m_StatusNotifierItem_data.ToolTip.description))) {
            return ret;
        }
    }
    if (0 > (ret = sd_bus_message_close_container(reply))) {
        return ret;
    }

    return ret;
}


static int
StatusNotifierWatcher_RegisterStatusNotifierItem_callback(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    assert(m_dbus.StatusNotifierItem_name_registered);

    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        log_sd_bus_error(sd_bus_message_get_error(message), "StatusNotifierWatcher::RegisterStatusNotifierItem");
        goto fail;
    }

    set_StatusNotifierItem_registered_with_watcher(true);
    DEBUG("RegisterStatusNotifierItem reply without error.\n");
    return 0;

fail:
    eprintf("Failed to register tray icon.\n");
    set_StatusNotifierItem_registered_with_watcher(false);
    return 0;
}


static bool
register_StatusNotifierItem_with_watcher() {
    m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback = sd_bus_slot_unref(m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback);
    int ret = sd_bus_call_method_async(
        m_dbus.bus, &m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback,
        "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
        StatusNotifierWatcher_RegisterStatusNotifierItem_callback, NULL,
        "s",
          m_StatusNotifierItem_name
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to call RegisterStatusNotifierItem");
        return false;
    }

    return true;
}


static int
Dbus_RequestName_callback__StatusNotifierItem(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    const char *error_name = NULL;
    if (sd_bus_message_is_method_error(message, error_name)) {
        log_sd_bus_error(sd_bus_message_get_error(message), "Could not register well-known name for StatusNotifierItem.");
        goto fail;
    }

    int ret;

    uint32_t request_result;
    ret = sd_bus_message_read(message, "u", &request_result);
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to read RequestName reply for binding StatusNotifierItem");
        goto fail;
    }

    enum {
        REPLY_PRIMARY_OWNER = 1,
        REPLY_IN_QUEUE = 2,
        REPLY_EXISTS = 3,
        REPLY_ALREADY_OWNER = 4,
    };
    if (request_result != REPLY_PRIMARY_OWNER && request_result != REPLY_ALREADY_OWNER) {
        eprintf("Couldn't bind desired StatusNotifierItem name\n");
        goto fail;
    }

    m_dbus.StatusNotifierItem_name_registered = true;

    if (!register_StatusNotifierItem_with_watcher()) {
        goto fail;
    }

    DEBUG("RegisterStatusNotifierItem reply without error.\n");
    return 0;
fail:
    scran_dbus_destroy_StatusNotifierItem();
    return 0;
}

static int
StatusNotifierItem_ContextMenu(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    // TODO
    return sd_bus_reply_method_return(message, "");
}

static int
StatusNotifierItem_Activate(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    start_grabbing_focus();
    return sd_bus_reply_method_return(message, "");
}

static int
StatusNotifierItem_SecondaryActivate(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    return sd_bus_reply_method_return(message, "");
}

static int
StatusNotifierItem_Scroll(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    return sd_bus_reply_method_return(message, "");
}

static const sd_bus_vtable m_StatusNotifierItem_vtable[] = {
    SD_BUS_VTABLE_START(0),

    SD_BUS_PROPERTY("Category",   "s", NULL, offsetof(struct StatusNotifierItem_data, Category),   SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id",         "s", NULL, offsetof(struct StatusNotifierItem_data, Id),         SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title",      "s", NULL, offsetof(struct StatusNotifierItem_data, Title),      SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status",     "s", NULL, offsetof(struct StatusNotifierItem_data, Status),     SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("WindowId",   "u", NULL, offsetof(struct StatusNotifierItem_data, WindowId),   SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ItemIsMenu", "b", NULL, offsetof(struct StatusNotifierItem_data, ItemIsMenu), SD_BUS_VTABLE_PROPERTY_CONST),
    // TODO: SD_BUS_PROPERTY("Menu",                "o",            NULL, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconName",   "s", NULL, offsetof(struct StatusNotifierItem_data, IconName),   SD_BUS_VTABLE_PROPERTY_CONST),
    // TODO: SD_BUS_PROPERTY("IconPixmap",          "a(iiay)",      NULL, 0, 0),
    SD_BUS_PROPERTY("ToolTip", STATUS_NOTIFIER_ITEM_TOOL_TIP_TYPE, StatusNotifierItem_ToolTip_get_property, 0, SD_BUS_VTABLE_PROPERTY_CONST),

    SD_BUS_METHOD("ContextMenu",       "ii", "", StatusNotifierItem_ContextMenu,       SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Activate",          "ii", "", StatusNotifierItem_Activate,          SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", StatusNotifierItem_SecondaryActivate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll",            "is", "", StatusNotifierItem_Scroll,            SD_BUS_VTABLE_UNPRIVILEGED),

    SD_BUS_SIGNAL("NewTitle",         "",  0),
    SD_BUS_SIGNAL("NewIcon",          "",  0),
    SD_BUS_SIGNAL("NewAttentionIcon", "",  0),
    SD_BUS_SIGNAL("NewOverlayIcon",   "",  0),
    SD_BUS_SIGNAL("NewToolTip",       "",  0),
    SD_BUS_SIGNAL("NewStatus",        "s", 0),

    SD_BUS_VTABLE_END
};

// Register tray icon
static bool
register_StatusNotifierItem()
{
    int ret;
    assert(m_dbus.bus != NULL);

    ret = sd_bus_add_object_vtable(
        m_dbus.bus, &m_dbus.StatusNotifierItem_slot_vtable,
        "/StatusNotifierItem", "org.kde.StatusNotifierItem",
        m_StatusNotifierItem_vtable, &m_StatusNotifierItem_data
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to add sd-bus vtable for StatusNotifierItem");
        goto fail;
    }

    // We add this *before* actually registering, to avoid potential race conditions.
    ret = sd_bus_add_match_async(
        m_dbus.bus, &m_dbus.StatusNotifierWatcher_slot_NameOwnerChanged_match,
        "type='signal',"
        "sender='org.freedesktop.DBus',"
        "path='/org/freedesktop/DBus',"
        "interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',"
        "arg0='org.kde.StatusNotifierWatcher'",
        Dbus_NameOwnerChanged_callback__StatusNotifierWatcher,
        Dbus_AddMatch_callback,
        NULL // TODO: Send name/description of current signal to the generic Dbus_AddMatch_callback?
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to add signal match for DBus.NameOwnerChanged for StatusNotifierWatcher");
        goto fail;
    }

    {
        int pid = getpid();
        ssize_t i = sizeof(STATUS_NOTIFIER_ITEM_NAME_BASE) - 1;
        assert(m_StatusNotifierItem_name[i-1] == '-');
        advance_itoa_7(pid, m_StatusNotifierItem_name, &i);
        m_StatusNotifierItem_name[i++] = '-';
        m_StatusNotifierItem_name[i++] = '1';
        m_StatusNotifierItem_name[i++] = '\0';
    }
    m_dbus.StatusNotifierItem_slot_RequestName_callback = sd_bus_slot_unref(m_dbus.StatusNotifierItem_slot_RequestName_callback);
    ret = sd_bus_request_name_async(
        m_dbus.bus, &m_dbus.StatusNotifierItem_slot_RequestName_callback,
        m_StatusNotifierItem_name,
        SD_BUS_NAME_ALLOW_REPLACEMENT | SD_BUS_NAME_REPLACE_EXISTING,
        Dbus_RequestName_callback__StatusNotifierItem, NULL
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to request well-known service name for StatusNotifierItem");
        goto fail;
    }

    return true;

fail:
    scran_dbus_destroy_StatusNotifierItem();
    return false;
}

bool
scran_dbus_init(int epoll_fd, int *timeout_ms)
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
        Notification_ActionInvoked_callback,
        Dbus_AddMatch_callback,
        NULL
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to register listener for Notification::ActionInvoked");
    }

    if (!register_StatusNotifierItem()) {
        eprintf("Warning: Failed to create tray icon\n.");
    }

    int dbus_fd = sd_bus_get_fd(m_dbus.bus);
    int _dbus_events = sd_bus_get_events(m_dbus.bus);
    struct epoll_event epoll_event = {
        .events = _dbus_events,
        .data.fd = dbus_fd,
    };
    if (-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dbus_fd, &epoll_event)) {
        eprintf("Failed to add D-Bus connection to epoll.\n");
        goto fail;
    }
    m_dbus.fd = dbus_fd;

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
    if (m_dbus.bus == NULL) {
        assert(m_dbus.fd == -1);
        *timeout_ms = -1;
        return;
    }

    // NOTE: Until sd_bus_process() returns 0, there might still be more work
    // left to do. Since our main loop is single-threaded, we limit how many
    // calls we allow per call, on the off-chance that a loop would block e.g.
    // a video capture frame.
    int process_ret;
    for (int i = 0; i < 8; ++i) {
        process_ret = sd_bus_process(m_dbus.bus, NULL);

        if (process_ret == 0) {
            break;
        }

        if (process_ret < 0) {
            log_sd_bus_ret_error(process_ret, "sd_bus_process() failed. Stopping SD-Bus connection.");
            goto fail;
        }
    }

    // sd_bus_get_events manpage implies we should always check for a new fd
    int dbus_fd = sd_bus_get_fd(m_dbus.bus);
    int _dbus_events = sd_bus_get_events(m_dbus.bus);
    struct epoll_event epoll_event = {
        .events = _dbus_events,
        .data.fd = dbus_fd,
    };
    if (dbus_fd == m_dbus.fd) {
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, m_dbus.fd, &epoll_event);
    } else {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_dbus.fd, NULL);
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dbus_fd, &epoll_event);
        m_dbus.fd = dbus_fd;
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


bool
scran_dbus_have_tray_icon()
{
    return m_dbus.StatusNotifierItem_registered_with_watcher;
}

void
scran_dbus_destroy_StatusNotifierItem()
{
    if (m_dbus.StatusNotifierItem_slot_vtable != NULL) {
        m_dbus.StatusNotifierItem_slot_vtable = sd_bus_slot_unref(m_dbus.StatusNotifierItem_slot_vtable);
    }
    if (m_dbus.StatusNotifierWatcher_slot_NameOwnerChanged_match != NULL) {
        m_dbus.StatusNotifierWatcher_slot_NameOwnerChanged_match = sd_bus_slot_unref(m_dbus.StatusNotifierWatcher_slot_NameOwnerChanged_match);
    }
    if (m_dbus.StatusNotifierItem_slot_RequestName_callback != NULL) {
        m_dbus.StatusNotifierItem_slot_RequestName_callback = sd_bus_slot_unref(m_dbus.StatusNotifierItem_slot_RequestName_callback);
    }
    if (m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback != NULL) {
        m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback = sd_bus_slot_unref(m_dbus.StatusNotifierWatcher_slot_RegisterStatusNotifierItem_callback);
    }

    if (m_dbus.bus != NULL) {
        if (m_dbus.StatusNotifierItem_name_registered) {
            sd_bus_release_name_async(
                m_dbus.bus, NULL, m_StatusNotifierItem_name,
                NULL, NULL // TODO: Should we care about handling this callback, other than maybe logging?
            );
            m_dbus.StatusNotifierItem_name_registered = false;
        }
    }

    m_dbus.StatusNotifierItem_registered_with_watcher = false;
}

void
scran_dbus_destroy(int epoll_fd)
{
    scran_dbus_destroy_StatusNotifierItem();

    if (m_dbus.bus != NULL) {
        sd_bus_flush_close_unref(m_dbus.bus);
        m_dbus.bus = NULL;
        eprintf("D-Bus connection closed.\n");
    }

    if (m_dbus.fd != -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, m_dbus.fd, NULL);
        m_dbus.fd = -1;
        DEBUG("Deleted D-Bus fd from epoll\n");
    }
}
