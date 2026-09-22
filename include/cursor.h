#ifndef SCRAN_CURSOR_H
#define SCRAN_CURSOR_H


#include <stdbool.h>

#include "compiler.h"

struct scran_output;


// FIXME: Change this to use dynamic allocation, so we don't have to limit the
// scale factor. It will also let the cursor tooltip's font rendering be more
// easily properly accounted for.
#define SCRAN_CURSOR_SIZE 20
// FIXME: This tooltip size is effectively a pure guess if we ever change to a
// different font. See also the fixme above.
#define SCRAN_CURSOR_TOOLTIP_SIZE (SCRAN_CURSOR_SIZE * 2)
#define SCRAN_CURSOR_TOOLTIP_GAP 2
// Leave some room for scaling up without needing to reallocate the shared memory buffers.
#define SCRAN_CURSOR_MAX_SCALE 4
#define SCRAN_CURSOR_BUFFER_WIDTH_PX  ((SCRAN_CURSOR_SIZE + SCRAN_CURSOR_TOOLTIP_GAP + SCRAN_CURSOR_TOOLTIP_SIZE) * SCRAN_CURSOR_MAX_SCALE)
#define SCRAN_CURSOR_BUFFER_HEIGHT_PX (SCRAN_CURSOR_SIZE * SCRAN_CURSOR_MAX_SCALE)

// This is only treated as a hard requirement because not all compositors have
// cursor-hiding implemented for software-cursors yet.
//     See [Hyprland #15883], [wlroots #5443].
_Static_assert(
    SCRAN_CURSOR_BUFFER_WIDTH_PX <= 256 && SCRAN_CURSOR_BUFFER_HEIGHT_PX <= 256,
    "Cursor buffer size exceeds common hardware-cursor limit of 256x256"
);

enum scran_cursor_tooltip {
    SCRAN_CURSOR_TOOLTIP_NONE = 0,
    SCRAN_CURSOR_TOOLTIP_FLIP_UI,
    SCRAN_CURSOR_N_TOOLTIPS,
} SCRAN_PACKED;

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
void cursor_set_tooltip(struct scran_output *output, enum scran_cursor_tooltip tooltip);


#endif
