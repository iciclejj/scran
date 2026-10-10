#ifndef SCRAN_DBUS_H
#define SCRAN_DBUS_H


#include <stdbool.h>


#define SCRAN_NOTIFICATION_PREFIX "Scran: "

void scran_portal_notify_file_saved(const char *path, bool incomplete);
void scran_portal_notify_error_(const char *title);
#define scran_portal_notify_error(msg) \
    scran_portal_notify_error_(SCRAN_NOTIFICATION_PREFIX "error: " msg)

void scran_dbus_update(int epoll_fd, int *timeout_ms);

bool scran_dbus_init(int epoll_fd, int *timeout_ms);
  void scran_dbus_destroy(int epoll_fd);

bool scran_tray_is_registered(void);
void scran_tray_destroy(void);


#endif
