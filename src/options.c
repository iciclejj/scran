#include <getopt.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>

#include "options.h"
#include "state.h"
#include "print.h"


#define SCRAN_DEFAULT_FILENAME_SIZE_MAX ((sizeof("scran-YYYYmmdd-HHMMSS.uuuu") - 1) + SCRAN_OUTPUT_FILE_EXTENSION_MAX)
static_assert(NAME_MAX >= SCRAN_DEFAULT_FILENAME_SIZE_MAX, "NAME_MAX must be >= SCRAN_DEFAULT_FILENAME_SIZE_MAX");

static inline void
create_timestamped_filename(
    char filename_out[NAME_MAX],
    const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]
) {
    struct timespec ts = { };
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm time_now_tm = { };
    localtime_r(&ts.tv_sec, &time_now_tm);

    // INFO: Assumes 4 decimal points (10khz) is the smallest safe divisor that
    // doesn't risk file-overwriting during rapid consecutive screenshots.
    const long tv_10khz = ts.tv_nsec / 100000;

    char strftime_buf[22];

    const size_t strftime_strlen = strftime(
        strftime_buf, sizeof(strftime_buf),
        "scran-%Y%m%d-%H%M%S", &time_now_tm
    );
    assert(strftime_strlen == sizeof(strftime_buf) - 1);

    const size_t filename_strlen = snprintf(
        filename_out, NAME_MAX,
        "%s.%04ld%s", strftime_buf, tv_10khz, file_extension
    );
    assert(filename_strlen <  SCRAN_DEFAULT_FILENAME_SIZE_MAX);
}

void
scran_update_output_filepath(
    const struct scran_options *st_options,
    const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]
) {
    create_timestamped_filename(st_options->output_path_filename_pointer, file_extension);
}

static inline bool
_mkdir_recursive(
    char dirpath[SCRAN_OUTPUT_DIRPATH_SIZE_MAX],
    size_t dirpath_strlen
) {
    DEBUG("_mkdir_recursive()\n");

    if (dirpath_strlen == 0) {
        return true;
    }

    // TODO: Make an NDEBUG_ASSERT macro? This shouldn't really ever happen, but
    // worth being safe here.
    if (dirpath_strlen > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX || dirpath[dirpath_strlen] != '\0') {
        eprintf("Error: _mkdir_recursive input length long. THIS IS A BUG, please open an issue.\n");
        return false;
    }

    char path_copy[SCRAN_OUTPUT_DIRPATH_SIZE_MAX];
    memcpy(path_copy, dirpath, dirpath_strlen + 1);

    assert(path_copy[dirpath_strlen] == '\0');

    size_t i = 0;
    while (i < dirpath_strlen) {
        assert(i == 0 || path_copy[i - 1] == '/');

        while (i < dirpath_strlen && path_copy[i] != '/') {
            ++i;
        }
        while (path_copy[i] == '/') {
            assert(i < dirpath_strlen);
            ++i;
        }

        const char tmp = path_copy[i];
        path_copy[i] = '\0';

        struct stat _statbuf;
        if (stat(path_copy, &_statbuf) == 0) {
            DEBUG("stat('%s')\n", path_copy);
            if (!S_ISDIR(_statbuf.st_mode)) {
                // TODO: %s is not a directory.
                eprintf("Error: output_path contains already existing non-directory file '%s'\n", path_copy);
                return false;
            }
        } else if (errno == ENOENT) {
            assert(path_copy[i] == '\0');
            DEBUG("mkdir('%s')\n", path_copy);
            if (mkdir(path_copy, 0755) != 0) {
                // TODO: Handle EEXIST in case directory was created in-between
                // stat() and mkdir()? Would need to ensure it's actually a dir.
                eprintf("Failed to create directory '%s': %s\n", path_copy, strerror(errno));
                return false;
            }
        } else {
            eprintf("output_path stat error for '%s': %s\n", path_copy, strerror(errno));
            return false;
        }

        path_copy[i] = tmp;
    }

#ifndef NDEBUG
{
    struct stat _statbuf = { };
    assert(stat(path_copy, &_statbuf) == 0 && S_ISDIR(_statbuf.st_mode));
}
#endif


    return true;
}

static inline bool
_cli_arg_output_path(
    struct scran_options *restrict st_options,
    const char *restrict arg
) {
    if (arg[0] == '-' && arg[1] == '\0') {
        st_options->output_to_stdout = true;
        return true;
    }

    if (arg[0] == '\0') {
        eprintf("Error: output_path cannot be empty.\n");
        return false;
    }

    size_t arg_strlen = strlcpy(st_options->output_path, arg, SCRAN_OUTPUT_FILEPATH_SIZE_MAX);

    if (arg_strlen > SCRAN_OUTPUT_FILEPATH_STRLEN_MAX) {
        eprintf("output_path is too long. Max length: %d (%d for directories)\n",
                SCRAN_OUTPUT_FILEPATH_SIZE_MAX, SCRAN_OUTPUT_DIRPATH_SIZE_MAX);
        return false;
    }

    assert(arg_strlen > 0);
    assert(arg[arg_strlen] == '\0');
    assert(st_options->output_path[arg_strlen] == '\0');


    bool is_existing_dirpath;
    struct stat _statbuf;
    const int _stat_ret = stat(st_options->output_path, &_statbuf);
    if (_stat_ret == 0) {
        is_existing_dirpath = S_ISDIR(_statbuf.st_mode);
    } else if (errno == ENOENT) {
        is_existing_dirpath = false;
    } else {
        eprintf("output_path stat error for '%s': %s\n", st_options->output_path, strerror(errno));
        return false;
    }

    bool _has_trailing_slash = st_options->output_path[arg_strlen - 1] == '/';
    bool is_dirpath_arg = is_existing_dirpath || _has_trailing_slash;
    //  !is_dirpath_arg => is_filepath_arg

    if (is_dirpath_arg) {
        if (arg_strlen > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX) {
            eprintf("output_path is too long. Max length: %d (%d for directories)\n",
                    SCRAN_OUTPUT_FILEPATH_SIZE_MAX, SCRAN_OUTPUT_DIRPATH_SIZE_MAX);
            return false;
        }

        if (!is_existing_dirpath) {
            if (!_mkdir_recursive(st_options->output_path, arg_strlen)) {
                eprintf("Failed to create directory '%s'\n", st_options->output_path);
                return false;
            }
        }

        char *filename_pointer = st_options->output_path + arg_strlen;
        if (*(filename_pointer - 1) != '/') {
            *filename_pointer++ = '/';
        }
        *filename_pointer = '\0'; // Should not be necessary, but just to be safe

        st_options->output_path_filename_pointer = filename_pointer;
    } else { // is filepath
        char *dirpath_end = st_options->output_path + arg_strlen;
        while (dirpath_end > st_options->output_path && *dirpath_end != '/') {
            --dirpath_end;
        }

        if (dirpath_end > st_options->output_path) {
            assert(*dirpath_end == '/');

            *dirpath_end = '\0';

            size_t _dirpath_strlen = dirpath_end - st_options->output_path;
            if (!_mkdir_recursive(st_options->output_path, _dirpath_strlen)) {
                eprintf("Failed to create directory '%s'\n", st_options->output_path);
                return false;
            }

            *dirpath_end = '/';
        }

        st_options->output_path_has_constant_filename = true;
    }

    assert(   st_options->output_path_has_constant_filename
           ^ (st_options->output_path_filename_pointer != NULL));

    return true;
}

#define SCRAN_USAGE "Usage: scran [options] [output_path]"

static const char help_string[] =
    SCRAN_USAGE "\n"
    "Capture images and videos\n"
    "\n"
    "Keymap\n"
    "  Left mouse button    Initialize and move selection\n"
    "  Right mouse button   Resize selection\n"
    "  Enter                Capture image and exit\n"
    "                         Stays alive in the background to handle clipboard,\n"
    "                         unless the -B option is provided.\n"
    "  Shift+Enter          Capture image\n"
    "  Space                Capture video (start/stop)\n"
    "  Tab                  Release focus (stop capturing inputs)\n"
    "                         SIGUSR1 to retake focus - see Signals section.\n"
    "  Escape               Exit scran, or stop video capture if in progress\n"
    "\n"
    "Positional arguments\n"
    // TODO: Once we implement desktop notifications, we should probably remove
    // the recursive directory structure creation by default, and just give an
    // error message notification that directory doesn't exist. (Maybe still keep
    // the functionality behind an --mkdir flag.)
    "  output_path   path to output file or directory.\n"
    "                output_path is -:\n"
    "                  -  scran writes to stdout (See also: -B)\n"
    "                output_path is an existing directory:\n"
    "                  -  scran writes to <output_path>/<default_filename>\n"
    "                output_path does not exist, but ends with '/':\n"
    "                  1. scran creates directory structure\n"
    "                  2. scran writes to <output_path>/<default_filename>\n"
    "                output_path does not exist:\n"
    "                  1. scran creates directory structure if necessary\n"
    "                  2. scran writes to <output_path>\n"
    "                  NOTE: the *exact* given file path is used for both image and video\n"
    "\n"
    "Options\n"
    "  -p   press-only mouse buttons (presses toggle pressed/released state)\n"
    "  -e   automatically capture and exit immediately after initial selection\n"
    "  -B   do not keep background process alive\n"
    "         Example: 'scran -B - | satty -f -'\n"
    "          By default, scran stays alive after exit to manage the clipboard\n"
    "         (until another process takes over, e.g. you copied some text in a web\n"
    "         browser). Useful if you want to pipe scran's output to an application\n"
    "         that is waiting for scran to fully exit.\n"
    "  -h   show this help message and exit\n"
    "\n"
    "Signals\n"
    "  Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with <Tab>.\n"
    "  - Example:            `pkill -SIGUSR1 scran`\n"
    "  - As sway keybinding: `bindsym Shift+Alt+Tab exec 'pkill -SIGUSR1 scran'`\n"
;

bool
scran_handle_args(int argc, char *const *argv)
{
    extern struct scran g_state;
    int opt;
    while ((opt = getopt(argc, argv, "peBh")) != -1) {
        switch (opt) {
        case 'p':
            g_state.seat.pointer_ctx.use_presses_only = true;
            break;
        case 'e':
            g_state.options.capture_and_exit_after_selection_init = true;
            break;
        case 'B':
            g_state.options.no_keepalive = true;
            break;
        case 'h':
            printf("%s", help_string);
            exit(EXIT_SUCCESS);
        default:
            eprintf(SCRAN_USAGE "\n\n" "Try scran -h for more information.\n");
            return false;
        }
    }
    // NOTE: getopt reorders argv and puts positional/non-option args at the end,
    // making optind point to them, unless POSIXLY_CORRECT or optstring[0] == '+'.
    const int i_posarg_0 = optind;

    if (argv[i_posarg_0] == NULL) {
        assert(0 == strcmp(g_state.options.output_path, SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH));
    } else {
        if (argv[i_posarg_0 + 1] != NULL) {
            eprintf("Error: Too many non-option arguments: ");
            for (int i = i_posarg_0; i < argc; ++i) {
                eprintf(" '%s'", argv[i]);
            }
            eprintf(".\n");
            return false;
        }

        // Just for some safety (compile-time initialized with default dir path):
        g_state.options.output_path[0] = '\0';
        g_state.options.output_path_filename_pointer = NULL;

        char *output_path_arg = argv[i_posarg_0];
        if (!_cli_arg_output_path(&g_state.options, output_path_arg)) {
            eprintf("Error: Failed parsing output_path.\n");
            return false;
        }
    }

    return true;
}

