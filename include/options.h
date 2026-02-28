#ifndef SCRAN_OPTIONS_H
#define SCRAN_OPTIONS_H


#include <assert.h>
#include <limits.h>

#include "state.h"


#define SCRAN_OUTPUT_DIRPATH_DEFAULT "/tmp/scran-capture"
static_assert(sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT) <= SCRAN_OUTPUT_DIRPATH_SIZE_MAX, "SCRAN_OUTPUT_DIRPATH_DEFAULT is too long");


void create_timestamped_filename(char filename_ret[NAME_MAX], const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]);
void scran_update_output_filepath(const struct scran_options *st_options, const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]);
bool scran_parse_args(struct scran_options *st_options, int argc, char *const *argv);


#endif
