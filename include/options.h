#ifndef SCRAN_OPTIONS_H
#define SCRAN_OPTIONS_H

#include "state.h"


#define SCRAN_OUTPUT_FILENAME_FORMATSTRING_DEFAULT "scran-%Y%m%d_%H%M%S.%U%E"

#define SLURP_STRING_SIZE (sizeof("99999,99999 99999x99999"))

bool scran_handle_args(int argc, char *const *argv);
bool scran_parse_slurp_string( char slurp_string[static SLURP_STRING_SIZE], struct BLRectI *result);
const char *scran_update_video_output_path(struct scran_options *options, const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX]);
const char *scran_update_image_output_path(struct scran_options *options, const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX]);


#endif
