#ifndef SCRAN_FREEZEFRAME_H
#define SCRAN_FREEZEFRAME_H


#include "state.h"


void freezeframe_capture_start(struct scran_output *st_output, scran_output_callback callback);
void freezeframe_capture_start_assume_callback_set(struct scran_output *st_output);
void freezeframe_capture_refresh(struct scran_output *st_output, scran_output_callback callback);
void freezeframe_capture_handle_frame_ready(struct scran_output *st_output);
void freezeframe_capture_handle_failed(struct scran_output *st_output, uint32_t reason);

void freezeframe_hide_if_showing(struct scran_output *st_output);

void freezeframe_surface_update_scale_size_viewport(struct scran_output *st_output);


#endif
