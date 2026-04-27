#include <getopt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <blend2d/blend2d.h>

#include "options.h"
#include "capture.h"
#include "state.h"
#include "print.h"


static inline void
_filename_advance_itoa_6(
    int integer,
    char *restrict fn,
    ssize_t *restrict i_fn
) {
    assert(integer <= 999999);

    int lo2 = integer % 100;
    int hi4 = integer / 100;

    int hi4_lo = hi4 % 100;
    int hi4_hi = hi4 / 100;

    fn[(*i_fn)++] = '0' + hi4_hi / 10;
    fn[(*i_fn)++] = '0' + hi4_hi % 10;
    fn[(*i_fn)++] = '0' + hi4_lo / 10;
    fn[(*i_fn)++] = '0' + hi4_lo % 10;
    fn[(*i_fn)++] = '0' + lo2 / 10;
    fn[(*i_fn)++] = '0' + lo2 % 10;
}

static inline void
_filename_advance_itoa_4(
    int integer,
    char *restrict fn,
    ssize_t *restrict i_fn
) {
    assert(integer <= 9999);

    int lo = integer % 100;
    int hi = integer / 100;

    fn[(*i_fn)++] = '0' + hi / 10;
    fn[(*i_fn)++] = '0' + hi % 10;
    fn[(*i_fn)++] = '0' + lo / 10;
    fn[(*i_fn)++] = '0' + lo % 10;
}

static inline void
_filename_advance_itoa_2(
    int integer,
    char *restrict fn,
    ssize_t *restrict i_fn
) {
    assert(integer <= 99);

    int lo = integer % 10;
    int hi = integer / 10;

    fn[(*i_fn)++] = '0' + hi;
    fn[(*i_fn)++] = '0' + lo;
}

static inline void
_filename_advance_ext(
    const char ext[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX],
    char *restrict fn,
    ssize_t *i_fn
) {
    assert(*i_fn < SCRAN_OUTPUT_FILENAME_SIZE_MAX - SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX);

    ssize_t i_ext = 0;
    while (i_ext < SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX && ext[i_ext] != '\0') {
        fn[(*i_fn)++] = ext[i_ext++];
    }
}

static inline bool
_create_filename(
    const char format[static restrict SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX],
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX],
    const struct timespec *ts,
    const struct tm *tm,
    char filename_out[static restrict SCRAN_OUTPUT_FILENAME_SIZE_MAX]
) {
    static const ssize_t format_max         = SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX;
    static const ssize_t file_extension_max = SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX;
    static const ssize_t filename_strlen_max   = SCRAN_OUTPUT_FILENAME_STRLEN_MAX;

    ssize_t i_out = 0;
    ssize_t i_fmt = 0;

    while (i_fmt < format_max) {
        if (format[i_fmt] == '\0') {
            filename_out[i_out++] = format[i_fmt++];
            break;
        } else if (format[i_fmt] != '%') {
            if (i_out >= filename_strlen_max) {
                goto filename_out_overflow;
            }

            filename_out[i_out++] = format[i_fmt++];
            continue;
        }

        assert(format[i_fmt] == '%');
        ++i_fmt;

        const char format_specifier = format[i_fmt];
        ++i_fmt;

        switch(format_specifier) {
        case 'Y':
            if (i_out > filename_strlen_max - 4) goto filename_out_overflow;
            _filename_advance_itoa_4(tm->tm_year + 1900, filename_out, &i_out);
            break;
        case 'm':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            _filename_advance_itoa_2(tm->tm_mon + 1, filename_out, &i_out);
            break;
        case 'd':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            _filename_advance_itoa_2(tm->tm_mday, filename_out, &i_out);
            break;
        case 'H':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            _filename_advance_itoa_2(tm->tm_hour, filename_out, &i_out);
            break;
        case 'M':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            _filename_advance_itoa_2(tm->tm_min , filename_out, &i_out);
            break;
        case 'S':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            _filename_advance_itoa_2(tm->tm_sec , filename_out, &i_out);
            break;
        case 'U':
            if (i_out > filename_strlen_max - 6) goto filename_out_overflow;
            _filename_advance_itoa_6(ts->tv_nsec / 1000, filename_out, &i_out);
            break;
        case 'E':
            if (i_out > filename_strlen_max - file_extension_max) goto filename_out_overflow;
            _filename_advance_ext(file_extension, filename_out, &i_out);
            break;
        // Escape-character:
        case '%':
            if (i_out > filename_strlen_max - 1) goto filename_out_overflow;
            filename_out[i_out++] = '%';
            break;
        default:
            eprintf("Error: Invalid format specifier: %c.\n", format_specifier);
            return false;
        }
    }

    if (i_fmt == format_max && format[i_fmt - 1] != '\0') {
        eprintf("Error: Format string overflow. THIS IS A BUG, please open an issue.\n");
        return false;
    }

    return true;

filename_out_overflow:
    eprintf("Error: Filename produced by format string is >%zd\n", filename_strlen_max);
    return false;
}

// For cheaply testing format string validity
// NOTE: Relies on create_filename's return value for error checking.
//       This function is mainly to bypass expensive syscalls and to make sure
//       max values for timespec etc. gets tested.
static inline bool
_create_filename_mock_time(
    const char format[static restrict SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX]
) {
    char _tmp[SCRAN_OUTPUT_FILENAME_SIZE_MAX];
    static const char _file_extension[SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX] = ".test";
    static const struct timespec _ts = {
        .tv_sec = 52582958239532, .tv_nsec = NSEC_PER_SEC - 1
    };
    static const struct tm _tm = {
        .tm_sec   = 60, .tm_min   = 59, .tm_hour  = 23,
        .tm_mday  = 31, .tm_mon   = 11, .tm_year  = 9999 - 1900,
        .tm_wday  = 6,  .tm_yday  = 365,
        .tm_isdst = 1,
    };

    if (!_create_filename(format, _file_extension, &_ts, &_tm, _tmp)) {
        return false;
    }

    return true;
}

static inline bool
_create_filename_current_time(
    const char format[static restrict SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX],
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX],
    char filename_out[static restrict SCRAN_OUTPUT_FILENAME_SIZE_MAX]
) {
    struct timespec ts_now = { };
    clock_gettime(CLOCK_REALTIME, &ts_now);

    struct tm tm_now = { };
    localtime_r(&ts_now.tv_sec, &tm_now);

    return _create_filename(format, file_extension, &ts_now, &tm_now, filename_out);
}


const char *
scran_update_output_filepath(
    struct scran_options *st_options,
    // XXX: Requiring callers to have pass >= this size array here is not
    // optimal, but provides us some easy safety guarantees from the compiler.
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX]
) {
    // TODO: NDEDBUG_ASSERT
    const size_t available_chars_for_filename = st_options->output_path
                                              + sizeof(st_options->output_path)
                                              - st_options->output_path_filename_pointer;
    if (available_chars_for_filename < SCRAN_OUTPUT_FILENAME_SIZE_MAX) {
        eprintf("Error: scran_update_output_filepath: filename pointer too deep. THIS IS A BUG, please open an issue.\n");
        exit(EXIT_FAILURE);
    }

    bool success = _create_filename_current_time(
        st_options->filename_format, file_extension, st_options->output_path_filename_pointer
    );

    // We verified the format string during init
    assert(success);

    return st_options->output_path;
}

bool
scran_parse_slurp_string(
    char slurp_string[static SLURP_STRING_SIZE],
    struct BLRectI *result
) {
    char *endptr = NULL;

    result->x = strtol(slurp_string, &endptr, 10);
    if (*endptr != ',')  return false;

    result->y = strtol(++endptr, &endptr, 10);
    if (*endptr != ' ')  return false;

    result->w = strtol(++endptr, &endptr, 10);
    if (*endptr != 'x')  return false;

    result->h = strtol(++endptr, &endptr, 10);
    if (*endptr != '\0') return false;

    return true;
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
    const struct scran_options *st_options,
    bool should_create
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
        if (!should_create) {
            eprintf("Error: output directory does not exist: '%s'\n", st_options->output_path);
            return false;
        }

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
    size_t format_strlen = strlcpy(st_options->filename_format, arg, SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX);

    if (format_strlen < 1) {
        eprintf("Error: filename cannot be empty.\n");
        return false;
    } else if (format_strlen > SCRAN_OUTPUT_FILENAME_FORMATSTRING_STRLEN_MAX) {
        eprintf("filename is too long. Max length: %d\n", SCRAN_OUTPUT_FILENAME_FORMATSTRING_STRLEN_MAX);
        return false;
    }

    // The create_filename function prints a descriptive error message.
    return _create_filename_mock_time(st_options->filename_format);
}

static inline bool
_handle_cli_arg_output_directory(
    struct scran_options *restrict st_options,
    const char *restrict arg
) {
    assert(sizeof(st_options->output_path) >= SCRAN_OUTPUT_DIRPATH_SIZE_MAX);
    size_t output_directory_strlen = strlcpy(st_options->output_path, arg, SCRAN_OUTPUT_DIRPATH_SIZE_MAX);

    if (output_directory_strlen < 1) {
        eprintf("Error: output_directory cannot be empty.\n");
        return false;
    } else if (output_directory_strlen > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX) {
        eprintf("Error: output_directory is too long. Max length: %d\n", SCRAN_OUTPUT_DIRPATH_STRLEN_MAX);
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
    "  Arrow keys           Move selection by one pixel\n"
    "  Escape               Exit scran, or stop video capture if in progress\n"
    "\n"
    "Arguments\n"
    // TODO: Once we implement desktop notifications, we should probably remove
    // the recursive directory structure creation by default, and just give an
    // error message notification that directory doesn't exist. (Maybe still keep
    // the functionality behind an --mkdir flag.)
    "  output_directory   path to output directory, or - (a hyphen) to write to stdout\n"
    "                        Directory will be created if it does not exist.\n"
    "                        See also -B if writing to stdout.\n"
    "                        NOTE:\n"
    "                          Other than \"- (a hyphen) to write to stdout\", the rest\n"
    "                          of this convenience argument's behavior is still subject\n"
    "                          to change. Please use -f, -d and SCRAN_OUTPUT_DIR if you\n"
    "                          need stable commands for keybindings or scripts.\n"
    "\n"
    "  -f   <filename_pattern>\n"
    "         Name of the file that will be placed in the output directory\n"
    "         Ignored if `output_directory` is - (stdout)\n"
    "         Expanded patterns:\n"
    "           %Y  Year  (4 digits)        %H  Hour         (00-23)\n"
    "           %m  Month (01-12)           %M  Minute       (00-59)\n"
    "           %d  Day   (01-31)           %S  Second       (00-59)\n"
    "                                       %U  Microsecond  (000000-999999)\n"
    "           %E  File extension (e.g. .png or .mp4)\n"
    "           %%  A literal '%' character\n"
    "         Default: "SCRAN_OUTPUT_FILENAME_FORMATSTRING_DEFAULT"\n"
    "  -d   set an existing directory as output directory\n"
    "         You may also use $SCRAN_OUTPUT_DIR (ignored if -d is passed).\n"
    "         Default directory is '"SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH"'. Scran will create it\n"
    "         automatically when needed.\n"
    "  -p   press-only mouse buttons (presses toggle pressed/released state)\n"
    "  -e   automatically capture and exit immediately after initial selection\n"
    "         Note: does not make -B redundant.\n"
    // TODO:
    // "  -ee  like -e, but ensure the scran process exits fully\n"
    // "         Equivalent to -Be"
    "  -A   disable audio capture (during video capture)\n"
    "         Note: audio capture requires PipeWire.\n"
    "  -B   do not keep background process alive\n"
    "         Example: 'scran -B - | satty -f -'\n"
    "          By default, scran stays alive after exit to manage the clipboard\n"
    "         (until another process takes over, e.g. you copied some text in a web\n"
    "         browser). Useful if you want to pipe scran's output to an application\n"
    "         that is waiting for scran to fully exit.\n"
    "         NOTE: This also disables notification interaction\n"
    "  -s   slurp: send selection as geometry string to standard output\n"
    "         Replaces/disables image capture.\n"
    "         Format: '<x>,<y> <width>x<height>'\n"
    "           x and y are coordinates in the global compositor space.\n"
    "         Equivalent to slurp's default output format\n"
    "           See https://wayland.emersion.fr/slurp/.\n"
    "           'scran -se' effectively emulates slurp's ui behavior\n"
    "  -g   \"<x>,<y> <width>x<height>\"\n"
    "         Pre-initialize selection using slurp-style geometry string\n"
    "         The area is clamped to the output containing the top-left corner.\n"
    "           Subject to change if/when scran will support cross-output capture.\n"
    "  -N   disable notifications\n"
    "  -h   show this help message and exit\n"
    "\n"
    "Signals\n"
    "  Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with <Tab>.\n"
    "  - Example:            `pkill -SIGUSR1 scran`\n"
    "  - As sway keybinding: `bindsym Shift+Alt+Tab exec 'pkill -SIGUSR1 scran'`\n"
    "\n"
    "v0.8.0\n"
;

bool
scran_handle_args(int argc, char *const *argv)
{
    extern struct scran g_state;
    char *opt_filename    = NULL;
    char *opt_output_directory = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "f:d:peABsg:Nh")) != -1) {
        switch (opt) {
        case 'f': opt_filename                                          = optarg; break;
        case 'd': opt_output_directory                                  = optarg; break;
        case 'p': g_state.seat.pointer_ctx.use_presses_only             = true;   break;
        case 'e': g_state.options.capture_and_exit_after_selection_init = true;   break;
        case 'A': g_state.options.disable_audio_capture                 = true;   break;
        case 'B': g_state.options.no_keepalive                          = true;   break;
        case 's': g_state.options.produce_slurp                         = true;   break;
        case 'g':
            {
                char consumable_slurp[SLURP_STRING_SIZE];

                if (strlcpy(consumable_slurp, optarg, sizeof(consumable_slurp))
                    >= sizeof(consumable_slurp)
                ) {
                    eprintf("Error: -g argument too long; max length: %lu."
                            " Please open an issue if you think the limit should be raised.\n",
                            SLURP_STRING_SIZE);
                    return false;
                }

                if (!scran_parse_slurp_string(
                        consumable_slurp,
                        &g_state.options.custom_initial_selection_global_coordinates
                    )
                ) {
                    eprintf("Error: Failed to parse geometry string.\n");
                    return false;
                }
                g_state.options.have_custom_initial_selection = true;
            }
            break;
        case 'N': g_state.options.no_notifications                      = true;   break;
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

    const char *arg_output_directory = argv[i_posarg++];

    if (i_posarg < argc) {
        eprintf("Error: Too many non-option arguments: ");
        for (int i = i_posarg; i < argc; ++i) {
            eprintf(" '%s'", argv[i]);
        }
        eprintf(".\n");
        return false;
    }

    const char *output_directory = NULL;
    bool should_create_output_dir = false;

    if (opt_output_directory && arg_output_directory) {
        eprintf("Error: Received both `-d` and `output_path`\n");
        return false;
    } else if (opt_output_directory) {
        output_directory = opt_output_directory;
    } else if (arg_output_directory) {
        if (arg_output_directory[0] == '-' && arg_output_directory[1] == '\0') {
            g_state.options.output_to_stdout = true;
        } else {
            output_directory = arg_output_directory;
            should_create_output_dir = true;
        }
    } else {
        const char *env_output_directory = getenv("SCRAN_OUTPUT_DIR");
        if (env_output_directory) {
            output_directory = env_output_directory;
        }
    }

    // Compile-time initialized
    assert(0 == strcmp(g_state.options.output_path, SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH));
    assert(g_state.options.output_path_filename_pointer == g_state.options.output_path + sizeof(SCRAN_OUTPUT_DIRPATH_DEFAULT_WITH_SLASH) - 1);
    if (output_directory != NULL) {
        // Just for some safety, since these are not zero-initialized
        g_state.options.output_path[0] = '\0';
        g_state.options.output_path_filename_pointer = NULL;

        if (!_handle_cli_arg_output_directory(&g_state.options, output_directory)) {
            return false;
        }
    }

    if (!g_state.options.output_to_stdout) {
        if (!_init_output_dir(&g_state.options, should_create_output_dir)) {
            return false;
        }
    }

    assert(0 == strcmp(g_state.options.filename_format, SCRAN_OUTPUT_FILENAME_FORMATSTRING_DEFAULT));
    if (opt_filename != NULL && !g_state.options.output_to_stdout) {
        if (!_handle_cli_arg_filename(&g_state.options, opt_filename)) {
            return false;
        }
    }

    return true;
}

