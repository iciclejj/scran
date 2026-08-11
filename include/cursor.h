#ifndef SCRAN_CURSOR_H
#define SCRAN_CURSOR_H


#include <stdbool.h>

struct scran_output;


#define SCRAN_CURSOR_WIDTH_HEIGHT 20

// Leave room for scaling up to 4x without reallocating the shared memory buffers.
#define SCRAN_CURSOR_BUFFER_WIDTH_HEIGHT_PX (SCRAN_CURSOR_WIDTH_HEIGHT * 4)

enum scran_cursor_theme {
    SCRAN_CURSOR_THEME_DEFAULT = 0,
    SCRAN_CURSOR_THEME_VIDEO_CAPTURE,
    SCRAN_CURSOR_N_THEMES,
};

bool init_premem__cursor(struct scran_output *st_output);
 void init_premem__cursor__destroy(struct scran_output *st_output);
bool init_postmem__cursor(struct scran_output *st_output);
 void init_postmem__cursor__destroy(struct scran_output *st_output);

bool cursor_reinit(struct scran_output *st_output);
void cursor_set_theme(struct scran_output *st_output, enum scran_cursor_theme theme);


#endif
