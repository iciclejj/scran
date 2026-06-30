#ifndef SCRAN_DBUS_H
#define SCRAN_DBUS_H


#include <stdbool.h>


void scran_portal_notify_file_saved(const char *path);
void scran_portal_open_file(const char *saved_file_path);

void scran_dbus_update(int epoll_fd, int *timeout_ms);

bool scran_dbus_init(int epoll_fd, int *timeout_ms);
  void scran_dbus_destroy(int epoll_fd);

bool scran_dbus_have_tray_icon(void);
void scran_dbus_destroy_StatusNotifierItem(void); // TODO: Rename this to _tray_icon?


#endif
