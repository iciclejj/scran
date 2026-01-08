#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>

#include "state.h"

bool start_capture(struct client_state_output *st_output);
void dispatch_capture_event_loop(struct client_state_output *st_output);

#endif
