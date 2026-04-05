#ifndef SCRAN_PORTALS_H
#define SCRAN_PORTALS_H


#include <stdbool.h>


bool scran_portal_init();
  void scran_portal_destroy();

void scran_portal_notify_file_saved(const char *path);


#endif
