#include <blend2d/blend2d.h>

#include "state.h"
#include "init.h"


bool
init_premem__datacontrol(struct scran_seat_datacontrol *st_datacontrol)
{
    bl_array_init(&st_datacontrol->data_to_send, BL_OBJECT_TYPE_ARRAY_UINT8);

    return true;
}

void
init_premem__datacontrol__destroy(struct scran_seat_datacontrol *st_datacontrol)
{
    bl_array_destroy(&st_datacontrol->data_to_send);
}

