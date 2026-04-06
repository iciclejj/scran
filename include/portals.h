#ifndef SCRAN_PORTALS_H
#define SCRAN_PORTALS_H


#include <stdbool.h>


void scran_portal_notify_file_saved(const char *path);

void scran_portal_update(int epoll_fd, int *timeout_ms);

bool scran_portal_init(int epoll_fd, int *timeout_ms);
  void scran_portal_destroy(int epoll_fd);


#endif
