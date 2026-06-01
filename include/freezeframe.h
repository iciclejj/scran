#ifndef SCRAN_FREEZEFRAME_H
#define SCRAN_FREEZEFRAME_H


#include "state.h"


void request_freezeframe(struct scran_output *st_output, freezeframe_callback callback);
void request_freezeframe_assume_callback_set(struct scran_output *st_output);

void refresh_freezeframe(struct scran_output *st_output, freezeframe_callback callback);

void freezeframe_unhide_selection_surface(struct scran_output *st_output);
void freezeframe_hide_surface(struct scran_output *st_output);
void freezeframe_hide_selection_surface(struct scran_output *st_output);

void update_freezeframe_scale_size_viewport(struct scran_output *st_output);


#endif
