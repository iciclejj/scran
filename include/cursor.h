#ifndef SCRAN_CURSOR_H
#define SCRAN_CURSOR_H


#include <stdbool.h>

#include "compiler.h"

struct scran_output;


#define SCRAN_CURSOR_WIDTH_HEIGHT 20
// Leave some room for scaling up without needing to reallocate the shared memory buffers.
#define SCRAN_CURSOR_MAX_SCALE 4
#define SCRAN_CURSOR_BUFFER_WIDTH_HEIGHT_PX (SCRAN_CURSOR_WIDTH_HEIGHT * SCRAN_CURSOR_MAX_SCALE)

// This is only treated as a hard requirement because not all compositors have
// cursor-hiding implemented for software-cursors yet.
//     See [Hyprland #15883], [wlroots #5443].
_Static_assert(
    SCRAN_CURSOR_BUFFER_WIDTH_HEIGHT_PX <= 256 && SCRAN_CURSOR_BUFFER_WIDTH_HEIGHT_PX <= 256,
    "Cursor buffer size exceeds common hardware-cursor limit of 256x256"
);


enum scran_cursor_theme {
    SCRAN_CURSOR_THEME_DEFAULT = 0,
    SCRAN_CURSOR_THEME_VIDEO_CAPTURE,
    SCRAN_CURSOR_N_THEMES,
} SCRAN_PACKED;

bool init_premem__cursor(struct scran_output *st_output);
 void init_premem__cursor__destroy(struct scran_output *st_output);
bool init_postmem__cursor(struct scran_output *st_output);
 void init_postmem__cursor__destroy(struct scran_output *st_output);

bool cursor_reinit(struct scran_output *st_output);
void cursor_set_theme(struct scran_output *st_output, enum scran_cursor_theme theme);


#endif
