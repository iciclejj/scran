#include <getopt.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#include "options.h"
#include "state.h"
#include "print.h"


// TODO: Maybe optimize this a bit (and/or make it a bit cleaner somehow).
//       Also ensure string/array safety. Either asserts or live.
void
create_timestamped_filename(
    char filename_ret[NAME_MAX],
    const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]
) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm time_now_tm;
    localtime_r(&ts.tv_sec, &time_now_tm);

    char *_filename = filename_ret;
    // TODO: Remove this eventually and just use asserts. Resulting filename
    // length is deterministic.
    size_t _name_max = NAME_MAX;

    const int chars_added_after_sec = strftime(_filename, _name_max, "scran-capture_%Y%m%d-%H%M%S", &time_now_tm);
    _filename += chars_added_after_sec;
    _name_max -= chars_added_after_sec;

    // INFO: Assumes 4 decimal points (10khz) is the smallest safe divisor that
    // doesn't risk file-overwriting during rapid consecutive screenshots.
    const long _tv_usec = ts.tv_nsec / 100000;
    const int chars_added_after_usec = snprintf(_filename, _name_max, ".%04ld", _tv_usec);
    _filename += chars_added_after_usec;
    _name_max -= chars_added_after_usec;

    // XXX: %z is a gnu extension. (Timezone offset.)
    const int chars_added_after_timezone = strftime(_filename, _name_max, "%z", &time_now_tm);
    _filename += chars_added_after_timezone;
    _name_max -= chars_added_after_timezone;

    snprintf(_filename, _name_max, "%s", file_extension);
}

void
scran_update_output_filepath(
    const struct scran_options *st_options,
    const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]
) {
    create_timestamped_filename(st_options->output_filepath_filename_pointer, file_extension);
}


static inline bool
_handle_dirpath(struct scran_options *restrict st_options, const char *restrict dirpath_arg)
{
    // TODO: Benchmark all the string storage/logic (including downstream
    // path/filename handling) once we have more options and maybe optimize.

    size_t dirpath_strlen = 0;
    {
        // output_filepath will only contain the dirpath part throughout this scope
        char *const _dirpath = st_options->output_filepath;

        if (dirpath_arg != NULL) {
            dirpath_strlen = strlcpy(_dirpath, dirpath_arg, SCRAN_OUTPUT_DIRPATH_SIZE_MAX);
            assert(dirpath_strlen != 0); // (Should have been ensured by being a getopt required arg)

            if (dirpath_strlen > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX) {
                eprintf("Supplied directory path is too long. Max length: %d\n", SCRAN_OUTPUT_DIRPATH_STRLEN_MAX);
                return false;
            }
        } else {
             memcpy( _dirpath,
                     SCRAN_OUTPUT_DIRPATH_DEFAULT,
                     sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT)
             );
             dirpath_strlen = sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT) - 1;
        }

        assert(_dirpath[dirpath_strlen] == '\0');
        if (mkdir(_dirpath, 0755) != 0 && errno != EEXIST) {
            eprintf("Failed to create directory '%s': %s\n", _dirpath, strerror(errno));
            return false;
        }

        assert(st_options->output_filepath == _dirpath);
    }

    char *_filename_pointer = st_options->output_filepath + dirpath_strlen;

    if (*(_filename_pointer - 1) != '/') {
        *_filename_pointer = '/';
        ++_filename_pointer;
    }

    assert(*_filename_pointer == '\0'); // Not really necessary, but just to be safe

    st_options->output_filepath_filename_pointer = _filename_pointer;

    return true;
}


bool
scran_handle_args(int argc, char *const *argv)
{
    extern struct scran g_state;
    char *dirpath_arg = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "d:ph")) != -1) {
        switch (opt) {
        case 'd':
            dirpath_arg = optarg;
            break;
        case 'p':
            g_state.seat.pointer_ctx.use_presses_only = true;
            break;
        case 'h':
            // TODO: exit with EXIT_SUCESS after printing the help string
        default:
            printf(
                "Usage: scran [options]\n"
                "Capture images and videos\n"
                "\n"
                "Keymap\n"
                "  Left mouse button    Initialize and move selection\n"
                "  Right mouse button   Resize selection\n"
                "  Enter                Capture image and exit\n"
                "  Shift+Enter          Capture image\n"
                "  Space                Capture video (start/stop)\n"
                "\n"
                "Options\n"
                "  -d   output-directory path\n"
                "  -p   press-only mouse buttons (presses toggle pressed/released state)\n"
                "  -h   print this help message\n"
            );
            return false;
        }
    }

    if (!_handle_dirpath(&g_state.options, dirpath_arg)) {
        return false;
    }

    return true;
}

