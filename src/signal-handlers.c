#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <stdatomic.h>

#include "state.h"
#include "signal-handlers.h"


// TODO: Maybe a separate signal struct to keep this more sanitary, at least if
// we start adding more handlers/touching more state.


void
sig_grab_focus(int signal_number)
{
#ifndef NDEBUG // TODO: debug wrapper for write()
    static char msg[] =  "SIGNAL HANDLER: sig_grab_focus\n";
    write(STDOUT_FILENO, msg, sizeof(msg)-1);
#endif

    g_state.sig_focus_requested = true;
}

