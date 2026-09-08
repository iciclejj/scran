#ifndef SCRAN_DBUS__H
#define SCRAN_DBUS__H


#include <stdbool.h>
#include <string.h>

#ifdef SCRAN_LIBSYSTEMD_SD_BUS
  #include <systemd/sd-bus.h>
#else
  #include <basu/sd-bus.h>
#endif

#include "print.h"


struct scran_dbus {
    sd_bus *bus;
    int fd;
};
extern struct scran_dbus g_dbus;


int dbus_reply_AddMatch(sd_bus_message *message, void *data, sd_bus_error *ret_error);


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

#endif
