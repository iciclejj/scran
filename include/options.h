#ifndef SCRAN_OPTIONS_H
#define SCRAN_OPTIONS_H


#include <assert.h>
#include <limits.h>

#include "state.h"


#define SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH "/tmp/scran-capture/"
static_assert(sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH) <= SCRAN_OUTPUT_DIRPATH_SIZE_MAX, "SCRAN_OUTPUT_DIRPATH_DEFAULT is too long");

#define SLURP_STRING_SIZE (sizeof("99999,99999 99999x99999"))

const char *scran_update_output_filepath(struct scran_options *st_options, const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]);
bool scran_handle_args(int argc, char *const *argv);
bool scran_parse_slurp_string( char slurp_string[static SLURP_STRING_SIZE], struct BLRectI *result);


#endif
