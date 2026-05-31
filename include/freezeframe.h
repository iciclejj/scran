#ifndef SCRAN_FREEZEFRAME_H
#define SCRAN_FREEZEFRAME_H


#include "state.h"


void request_freezeframe(struct scran_output *st_output);
void refresh_freezeframe(struct scran_output *st_output);
void refresh_freezeframe__finally(struct scran_output *st_output);
void hide_freezeframe_surface(struct scran_output *st_output);
void update_freezeframe_scale_size_viewport(struct scran_output *st_output);


#endif
