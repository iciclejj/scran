#include <assert.h>
#include <stddef.h>
#include <unistd.h>

#include "dbus.h"
#include "seat.h"
#include "selection.h"
#include "util/util.h"

#include "./dbus_.h"
#include "./tray_.h"


struct scran_status_notifier_watcher {
    sd_bus_slot *slot_RegisterStatusNotifierItem_callback;
    sd_bus_slot *slot_NameOwnerChanged_match;
};

struct scran_status_notifier_item {
    sd_bus_slot *slot_vtable;
    sd_bus_slot *slot_RequestName_callback;

    bool         name_registered;         // Name registered with DBus
    bool         registered_with_watcher; // Item registered with StatusNotifierWatcher
};

// (Scran doesn't own the watcher, only the item)
static struct scran_status_notifier_item    m_sni = {0};
static struct scran_status_notifier_watcher m_snw = {0};


#define STATUS_NOTIFIER_ITEM_NAME_BASE "org.kde.StatusNotifierItem-"
#define STATUS_NOTIFIER_ITEM_ICON_NAME "camera-photo"
static char m_StatusNotifierItem_name[64] = STATUS_NOTIFIER_ITEM_NAME_BASE;


static struct StatusNotifierItem_data {
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


struct StatusNotifierItem_icon_pixmap {
    int  width;
    int  height;
    size_t _data_size;
    const uint8_t *data;
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
    scran_focus_grab();
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

bool
scran_tray_is_registered()
{
    return m_sni.registered_with_watcher;
}

static inline void
set_StatusNotifierItem_registered_with_watcher(bool registered)
{
    m_sni.registered_with_watcher = registered;
    update_focus_keymap_texts(scran_tray_is_registered());
}

static int
StatusNotifierWatcher_RegisterStatusNotifierItem_callback(
    struct sd_bus_message *message,
    void *userdata,
    sd_bus_error *error
) {
    assert(m_sni.name_registered);

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
    m_snw.slot_RegisterStatusNotifierItem_callback = sd_bus_slot_unref(m_snw.slot_RegisterStatusNotifierItem_callback);
    int ret = sd_bus_call_method_async(
        g_dbus.bus, &m_snw.slot_RegisterStatusNotifierItem_callback,
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
        m_snw.slot_RegisterStatusNotifierItem_callback = sd_bus_slot_unref(
            m_snw.slot_RegisterStatusNotifierItem_callback
        );
    }

    if (new_owner[0] != '\0') { // New owner exists
        if (m_sni.name_registered) {
            register_StatusNotifierItem_with_watcher(); // Try to re-register with new owner
        }
    }

    return 0;
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

    m_sni.name_registered = true;

    if (!register_StatusNotifierItem_with_watcher()) {
        goto fail;
    }

    DEBUG("RegisterStatusNotifierItem reply without error.\n");
    return 0;
fail:
    scran_tray_destroy();
    return 0;
}

// Register tray icon
bool
scran_tray_init()
{
    int ret;
    assert(g_dbus.bus != NULL);

    ret = sd_bus_add_object_vtable(
        g_dbus.bus, &m_sni.slot_vtable,
        "/StatusNotifierItem", "org.kde.StatusNotifierItem",
        m_StatusNotifierItem_vtable, &m_StatusNotifierItem_data
    );
    if (ret < 0) {
        log_sd_bus_ret_error(ret, "Failed to add sd-bus vtable for StatusNotifierItem");
        goto fail;
    }

    // We add this *before* actually registering, to avoid potential race conditions.
    ret = sd_bus_add_match_async(
        g_dbus.bus, &m_snw.slot_NameOwnerChanged_match,
        "type='signal',"
        "sender='org.freedesktop.DBus',"
        "path='/org/freedesktop/DBus',"
        "interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',"
        "arg0='org.kde.StatusNotifierWatcher'",
        Dbus_NameOwnerChanged_callback__StatusNotifierWatcher,
        Dbus_AddMatch_callback__generic,
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
    m_sni.slot_RequestName_callback = sd_bus_slot_unref(m_sni.slot_RequestName_callback);
    ret = sd_bus_request_name_async(
        g_dbus.bus, &m_sni.slot_RequestName_callback,
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
    scran_tray_destroy();
    return false;
}

void
scran_tray_destroy()
{
    if (m_sni.slot_vtable != NULL) {
        m_sni.slot_vtable = sd_bus_slot_unref(m_sni.slot_vtable);
    }
    if (m_snw.slot_NameOwnerChanged_match != NULL) {
        m_snw.slot_NameOwnerChanged_match = sd_bus_slot_unref(m_snw.slot_NameOwnerChanged_match);
    }
    if (m_sni.slot_RequestName_callback != NULL) {
        m_sni.slot_RequestName_callback = sd_bus_slot_unref(m_sni.slot_RequestName_callback);
    }
    if (m_snw.slot_RegisterStatusNotifierItem_callback != NULL) {
        m_snw.slot_RegisterStatusNotifierItem_callback = sd_bus_slot_unref(m_snw.slot_RegisterStatusNotifierItem_callback);
    }

    if (g_dbus.bus != NULL) {
        if (m_sni.name_registered) {
            sd_bus_release_name_async(
                g_dbus.bus, NULL, m_StatusNotifierItem_name,
                NULL, NULL // TODO: Should we care about handling this callback, other than maybe logging?
            );
            m_sni.name_registered = false;
        }
    }

    m_sni.registered_with_watcher = false;
}
