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

const char *
scran_update_output_filepath(
    struct scran_options *st_options,
    const char file_extension[SCRAN_OUTPUT_FILE_EXTENSION_MAX]
) {
    // TODO: NDEDBUG_ASSERT
    const size_t available_chars_for_filename = st_options->output_path
                                              + sizeof(st_options->output_path)
                                              - st_options->output_path_filename_pointer;

    assert(sizeof(st_options->filename) == SCRAN_OUTPUT_FILENAME_SIZE_MAX);
    if (available_chars_for_filename < SCRAN_OUTPUT_FILENAME_SIZE_MAX) {
        eprintf("Error: scran_update-output_filepath: filename pointer too deep. THIS IS A BUG, please open an issue.\n");
        exit(EXIT_FAILURE);
    }

    const bool have_custom_filename = st_options->filename[0] != '\0';
    if (have_custom_filename) {
        // XXX: Custom filename is primarily intended for user-supplied format
        //      strings, so we don't bother optimizing away repeated copies.
        //      TODO: Implement said format strings (and remove this XXX)
        size_t filename_strlen = strlcpy(
            st_options->output_path_filename_pointer,
            st_options->filename,
            available_chars_for_filename
        );
    } else {
        create_timestamped_filename(st_options->output_path_filename_pointer, file_extension);
    }

    return st_options->output_path;
}

static inline bool
_mkdir_recursive(
    const char dirpath[SCRAN_OUTPUT_DIRPATH_SIZE_MAX],
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
_init_output_dir(
    const struct scran_options *st_options
) {
    bool output_directory_exists;
    {
        struct stat _statbuf;
        const int _stat_ret = stat(st_options->output_path, &_statbuf);
        if (_stat_ret == 0) {
            output_directory_exists = S_ISDIR(_statbuf.st_mode);
        } else if (errno == ENOENT) {
            output_directory_exists = false;
        } else {
            eprintf("output_directory stat error for '%s': %s\n", st_options->output_path, strerror(errno));
            return false;
        }
    }
    if (!output_directory_exists) {
        const size_t output_directory_strlen = st_options->output_path_filename_pointer
                                             - st_options->output_path;
        assert(st_options->output_path[output_directory_strlen] == '\0');

        if (!_mkdir_recursive(st_options->output_path, output_directory_strlen)) {
            eprintf("Failed to create directory '%s'\n", st_options->output_path);
            return false;
        }
    }

    return true;
}

static inline bool
_handle_cli_arg_filename(
    struct scran_options *restrict st_options,
    const char *restrict arg
) {
    size_t filename_strlen = strlcpy(st_options->filename, arg, SCRAN_OUTPUT_FILENAME_SIZE_MAX);

    if (filename_strlen < 1) {
        eprintf("Error: filename cannot be empty.\n");
        return false;
    } else if (filename_strlen > SCRAN_OUTPUT_FILENAME_STRLEN_MAX) {
        eprintf("filename is too long. Max length: %d\n", SCRAN_OUTPUT_FILENAME_STRLEN_MAX);
        return false;
    }

    return true;
}

static inline bool
_handle_cli_arg_output_directory(
    struct scran_options *restrict st_options,
    const char *restrict arg
) {
    if (arg[0] == '-' && arg[1] == '\0') {
        st_options->output_to_stdout = true;
        return true;
    }

    assert(sizeof(st_options->output_path) >= SCRAN_OUTPUT_DIRPATH_SIZE_MAX);
    size_t output_directory_strlen = strlcpy(st_options->output_path, arg, SCRAN_OUTPUT_DIRPATH_SIZE_MAX);

    if (output_directory_strlen < 1) {
        eprintf("Error: output_directory cannot be empty.\n");
        return false;
    } else if (output_directory_strlen > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX) {
        eprintf("output_directory is too long. Max length: %d\n", SCRAN_OUTPUT_DIRPATH_STRLEN_MAX);
        return false;
    }

    char *filename_pointer = st_options->output_path + output_directory_strlen;
    if (*(filename_pointer - 1) != '/') {
        *filename_pointer++ = '/';
    }
    *filename_pointer = '\0';
    st_options->output_path_filename_pointer = filename_pointer;

    return true;
}

#define SCRAN_USAGE "Usage: scran [options...] [output_directory]"

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
    "  output_directory   path to output directory\n"
    "                       Directory will be created if it does not exist.\n"
    "                       If set to -, scran writes to stdout (see also -B)\n"
    "\n"
    "Options\n"
    "  -f   output filename\n"
    "         Name of the file that will be placed inside of `output_directory`\n"
    "         Ignored if output_directory is - (stdout)\n"
    "  -p   press-only mouse buttons (presses toggle pressed/released state)\n"
    "  -e   automatically capture and exit immediately after initial selection\n"
    // TODO:
    // "  -ee  like -e, but ensure the scran process exits fully\n"
    // "         Equivalent to -Be"
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
    char *opt_filename = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "f:peBh")) != -1) {
        switch (opt) {
        case 'f': opt_filename                                          = optarg; break;
        case 'p': g_state.seat.pointer_ctx.use_presses_only             = true;   break;
        case 'e': g_state.options.capture_and_exit_after_selection_init = true;   break;
        case 'B': g_state.options.no_keepalive                          = true;   break;
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
    int i_posarg = optind;

    const char *output_directory_arg = argv[i_posarg++];

    if (i_posarg < argc) {
        eprintf("Error: Too many non-option arguments: ");
        for (int i = i_posarg; i < argc; ++i) {
            eprintf(" '%s'", argv[i]);
        }
        eprintf(".\n");
        return false;
    }

    // Compile-time initialized
    assert(0 == strcmp(g_state.options.output_path, SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH));
    assert(g_state.options.output_path_filename_pointer == g_state.options.output_path + sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH) - 1);
    if (output_directory_arg != NULL) {
        // Just for some safety, since these are not zero-initialized
        g_state.options.output_path[0] = '\0';
        g_state.options.output_path_filename_pointer = NULL;

        if (!_handle_cli_arg_output_directory(&g_state.options, output_directory_arg)) {
            eprintf("Error: Failed parsing output_path.\n");
            return false;
        }
    }

    if (!g_state.options.output_to_stdout) {
        if (!_init_output_dir(&g_state.options)) {
            return false;
        }
    }

    if (opt_filename != NULL && !g_state.options.output_to_stdout) {
        if (!_handle_cli_arg_filename(&g_state.options, opt_filename)) {
            return false;
        }
    }

    return true;
}

