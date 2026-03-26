#ifndef SCRAN_CLIPBOARD_H
#define SCRAN_CLIPBOARD_H


#include <blend2d/blend2d.h>

#include "state.h"


#define SCRAN_MIME_TYPE_FILEPATH "text/uri-list"


bool update_clipboard(struct scran_seat_datacontrol *datacontrol, BLArrayCore *data, const char *data_mime_type, const char *filepath);


#endif
