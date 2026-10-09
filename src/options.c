#include <getopt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>

#include <blend2d/blend2d.h>

#include "xdg-user-dir-lookup.h"

#include "options.h"
#include "capture.h"
#include "state.h"
#include "print.h"
#include "util/util.h"


static inline void
advance_filename_file_extension(
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
create_filename(
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
            advance_itoa_4(tm->tm_year + 1900, filename_out, &i_out);
            break;
        case 'm':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            advance_itoa_2(tm->tm_mon + 1, filename_out, &i_out);
            break;
        case 'd':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            advance_itoa_2(tm->tm_mday, filename_out, &i_out);
            break;
        case 'H':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            advance_itoa_2(tm->tm_hour, filename_out, &i_out);
            break;
        case 'M':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            advance_itoa_2(tm->tm_min , filename_out, &i_out);
            break;
        case 'S':
            if (i_out > filename_strlen_max - 2) goto filename_out_overflow;
            advance_itoa_2(tm->tm_sec , filename_out, &i_out);
            break;
        case 'U':
            if (i_out > filename_strlen_max - 6) goto filename_out_overflow;
            advance_itoa_6(ts->tv_nsec / 1000, filename_out, &i_out);
            break;
        case 'E':
            if (i_out > filename_strlen_max - file_extension_max) goto filename_out_overflow;
            advance_filename_file_extension(file_extension, filename_out, &i_out);
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
create_filename_mock_time(
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

    if (!create_filename(format, _file_extension, &_ts, &_tm, _tmp)) {
        return false;
    }

    return true;
}

static inline bool
create_filename_current_time(
    const char format[static restrict SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX],
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX],
    char filename_out[static restrict SCRAN_OUTPUT_FILENAME_SIZE_MAX]
) {
    struct timespec ts_now = { };
    clock_gettime(CLOCK_REALTIME, &ts_now);

    struct tm tm_now = { };
    localtime_r(&ts_now.tv_sec, &tm_now);

    return create_filename(format, file_extension, &ts_now, &tm_now, filename_out);
}


static const char *
scran_update_output_filepath(
    struct scran_options *options,
    struct scran_write_path *path,
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX]
) {
    if (path->filename_offset <= 0
        || SCRAN_OUTPUT_FILEPATH_SIZE_MAX - path->filename_offset < SCRAN_OUTPUT_FILENAME_SIZE_MAX
    ) {
        eprintf(
            "Error: scran_update_output_filepath: invalid offset: %zd. THIS IS A BUG, please open an issue.\n",
            path->filename_offset
        );
        exit(EXIT_FAILURE);
    }

    bool success = create_filename_current_time(
        options->filename_format,
        file_extension,
        path->str + path->filename_offset
    );
    (void)success;
    assert(success); // We verified the format string during init

    return path->str;
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
mkdir_recursive(struct scran_write_path *path)
{
    DEBUG("_mkdir_recursive()\n");

    if (path->filename_offset == 0) {
        return true;
    }

    // TODO: Make an NDEBUG_ASSERT macro? This shouldn't really ever happen, but
    // worth being safe here.
    if (path->filename_offset > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX || path->str[path->filename_offset] != '\0') {
        eprintf("Error: _mkdir_recursive input length long. THIS IS A BUG, please open an issue.\n");
        return false;
    }

    char path_copy[SCRAN_OUTPUT_DIRPATH_SIZE_MAX];
    memcpy(path_copy, path->str, path->filename_offset + 1);

    assert(path_copy[path->filename_offset] == '\0');

    ssize_t i = 0;
    while (i < path->filename_offset) {
        assert(i == 0 || path_copy[i - 1] == '/');

        while (i < path->filename_offset && path_copy[i] != '/') {
            ++i;
        }
        while (path_copy[i] == '/') {
            assert(i < path->filename_offset);
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
ensure_directory_exists(struct scran_write_path *path)
{
    // Save/restore this just so the caller doesn't have to care
    const char filename_offset_char = path->str[path->filename_offset];
    path->str[path->filename_offset] = '\0';

    bool ok = false;
    bool output_directory_exists = false;

    {
        struct stat statbuf;
        if (!stat(path->str, &statbuf)) {
            output_directory_exists = S_ISDIR(statbuf.st_mode);
        } else if (errno == ENOENT) {
            output_directory_exists = false;
        } else {
            eprintf("output_directory stat error for '%s': %s\n", path->str, strerror(errno));
            goto done;
        }
    }
    if (!output_directory_exists) {
        if (!path->should_mkdir) {
            eprintf("Error: output directory does not exist: '%s'\n", path->str);
            goto done;
        }

        if (!mkdir_recursive(path)) {
            eprintf("Failed to create directory '%s'\n", path->str);
            goto done;
        }
    }
    ok = true;
done:
    path->str[path->filename_offset] = filename_offset_char;
    return ok;
}

const char *
scran_prepare_image_output_path(
    struct scran_options *options,
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX]
) {
    if (!ensure_directory_exists(&options->image_path)) {
        return NULL;
    }
    return scran_update_output_filepath(
        options,
        &options->image_path,
        file_extension
    );
}

const char *
scran_prepare_video_output_path(
    struct scran_options *options,
    const char file_extension[static restrict SCRAN_OUTPUT_FILE_EXTENSION_SIZE_MAX]
) {
    if (!ensure_directory_exists(&options->video_path)) {
        return NULL;
    }
    return scran_update_output_filepath(
        options,
        &options->video_path,
        file_extension
    );
}

static inline bool
handle_cli_arg_filename(
    struct scran_options *restrict options,
    const char *restrict arg
) {
    size_t format_strlen = strlcpy(options->filename_format, arg, SCRAN_OUTPUT_FILENAME_FORMATSTRING_SIZE_MAX);

    if (format_strlen < 1) {
        eprintf("Error: filename cannot be empty.\n");
        return false;
    } else if (format_strlen > SCRAN_OUTPUT_FILENAME_FORMATSTRING_STRLEN_MAX) {
        eprintf("filename is too long. Max length: %d\n", SCRAN_OUTPUT_FILENAME_FORMATSTRING_STRLEN_MAX);
        return false;
    }

    // The create_filename function prints a descriptive error message.
    return create_filename_mock_time(options->filename_format);
}

static inline bool
set_output_directory(
    struct scran_write_path *path,
    const char *dir,
    const char *subdir,
    ssize_t     subdir_strlen
) {
    assert(dir);
    char *out = path->str;
    size_t out_strlen = 0;

    // TODO: Maybe strip "./" etc

    if (dir[0] != '/') {
        if (!getcwd(out, SCRAN_OUTPUT_DIRPATH_SIZE_MAX)) {
            eprintf("Error: Can't resolve relative output directory '%s': %s.\n", dir, strerror(errno));
            return false;
        }
        out_strlen += strlen(out);
        if (out[out_strlen - 1] != '/') {
            out[out_strlen++] = '/';
        }
    }

    const size_t dir_strlen = strlcpy(out + out_strlen, dir, SCRAN_OUTPUT_DIRPATH_SIZE_MAX - out_strlen);
    if (dir_strlen < 1) {
        eprintf("Error: output directory cannot be empty.\n");
        return false;
    }
    out_strlen += dir_strlen;

    bool dir_needs_slash = dir[dir_strlen - 1] != '/';
    bool subdir_needs_slash = subdir && subdir[subdir_strlen - 1] != '/';
    const size_t out_strlen_final = out_strlen + dir_needs_slash + subdir_strlen + subdir_needs_slash;

    if (out_strlen_final > SCRAN_OUTPUT_DIRPATH_STRLEN_MAX) {
        eprintf(
            "Error: output_directory is too long. Max length: %zu/%zu\n",
            out_strlen_final, (size_t)SCRAN_OUTPUT_DIRPATH_STRLEN_MAX
        );
        return false;
    }

    if (dir_needs_slash) {
        out[out_strlen++] = '/';
        out[out_strlen] = '\0';
    }

    if (subdir) {
        memcpy(out + out_strlen, subdir, subdir_strlen + 1);
        out_strlen += subdir_strlen;

        if (subdir_needs_slash) {
            out[out_strlen++] = '/';
            out[out_strlen] = '\0';
        }
    }

    assert(out_strlen == out_strlen_final);
    path->filename_offset = out_strlen_final;
    return true;
}

static inline bool
set_default_image_output_directory(struct scran_write_path *path)
{
    const char *dir           = NULL;
    char       *xdg_dir       = NULL;
    const char *subdir        = NULL;
    ssize_t     subdir_strlen = 0;

    if ((dir = getenv("XDG_SCREENSHOTS_DIR")) && dir[0] == '/') {
        // Non-standard environment override; require an absolute path.
    } else if ((xdg_dir = xdg_user_dir_lookup_with_fallback("PICTURES", NULL))) {
        static const char _subdir[] = "Screenshots";
        dir = xdg_dir;
        subdir = _subdir;
        subdir_strlen = sizeof(_subdir) - 1;
    } else if ((dir = getenv("HOME")) && dir[0]) {
        static const char _subdir[] = "Pictures/Screenshots";
        subdir = _subdir;
        subdir_strlen = sizeof(_subdir) - 1;
    } else {
        dir = NULL;
    }

    bool ok = false;
    if (dir) {
        ok = set_output_directory(path, dir, subdir, subdir_strlen);
    } else {
        eprintf("Error: No image output directory candidates found. See scran -h.\n");
    }

    // TODO: Consider editing the xdg lookup functions to not need dynamic
    // allocation, or at least to not have to re-read the file and re-allocate
    // for every additional looked-up directory.
    free(xdg_dir);

    return ok;
}

static inline bool
set_default_video_output_directory(struct scran_write_path *path)
{
    const char *dir            = NULL;
    char       *xdg_dir        = NULL;
    const char *subdir         = NULL;
    ssize_t     subdir_strlen  = 0;

    if ((xdg_dir = xdg_user_dir_lookup_with_fallback("VIDEOS", NULL))) {
        static const char _subdir[] = "Screencasts";
        dir = xdg_dir;
        subdir = _subdir;
        subdir_strlen = sizeof(_subdir) - 1;
    } else if ((dir = getenv("HOME")) && dir[0]) {
        static const char _subdir[] = "Videos/Screencasts";
        subdir = _subdir;
        subdir_strlen = sizeof(_subdir) - 1;
    } else {
        dir = NULL;
    }

    bool ok = false;
    if (dir) {
        ok = set_output_directory(path, dir, subdir, subdir_strlen);
    } else {
        eprintf("Error: No video output directory candidates found. See scran -h.\n");
    }

    // TODO: Consider editing the xdg lookup functions to not need dynamic
    // allocation, or at least to not have to re-read the file and re-allocate
    // for every additional looked-up directory.
    free(xdg_dir);

    return ok;
}


#define SCRAN_USAGE    "Usage: scran [options...] [output_directory]"

static const char help_string[] =
    SCRAN_USAGE "\n"
    "Capture images and videos\n"
    "\n"
    "Keymap\n"
    "  Left mouse button    Initialize and move selection\n"
    "  Right mouse button   Resize selection\n"
    "  S                    Move UI inside selection\n"
    "                         Useful when recording fullscreen or the UI is otherwise clipping\n"

    "  Enter                Capture image and exit\n"
    "                         Stays alive in the background to handle clipboard,\n"
    "                         unless the -B option is provided.\n"
    "  Shift+Enter          Capture image\n"
    "  Space                Capture video with audio\n"
    "  Shift+Space          Capture video without audio\n"
    "  Z                    Toggle screen freeze\n"
    "  Arrow keys           Move selection by one pixel\n"
    "  Tab                  Release focus (stop capturing inputs)\n"
    "                         Click tray icon or send SIGUSR1 to scran to retake focus\n"
    "                         (see Signals section).\n"
    "  Escape               Exit scran, or stop video capture if in progress\n"
    "\n"
    "Arguments\n"
    // TODO: Maybe remove the recursive directory structure creation by default,
    // and just give an error message notification that directory doesn't exist.
    // (Maybe still keep the functionality behind an --mkdir flag.)
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
    "         If a directory does not exist, scran will exit with an error.\n"
    "         See the 'Output directories' section below for defaults.\n"
    "  -p   press-only mouse buttons (presses toggle pressed/released state)\n"
    "  -e   automatically capture and exit immediately after initial selection\n"
    "         Note: does not make -B redundant.\n"
    // TODO:
    // "  -ee  like -e, but ensure the scran process exits fully\n"
    // "         Equivalent to -Be"
    "  -A   disable audio capture (during video capture)\n"
    "         Note: audio capture requires PipeWire.\n"
    "  -C   disable cursor capture\n"
    "  -c   capture all cursors, including scran's cursor and tooltip\n"
    "         By default, capture cursors only while scran has no pointer focus.\n"
    "         If both -c and -C are given, the last one takes effect.\n"
    "  -B   do not keep background process alive\n"
    "         Example: 'scran -B - | satty -f -'\n"
    "          By default, scran stays alive after exit to manage the clipboard\n"
    "         (until another process takes over, e.g. you copied some text in a web\n"
    "         browser). Useful if you want to pipe scran's output to an application\n"
    "         that is waiting for scran to fully exit.\n"
    "         NOTE: This also disables notification interaction\n"
    "  -z   automatically freeze the display at startup\n"
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
    "  -U   hide UI (partial)\n"
    "  -UU  hide UI (full)\n"
    "  -v   show version and exit\n"
    "  -h   show this help message and exit\n"
    "\n"
    "Output directories, in order of priority:\n"
    "  1. Images and videos: -d <directory> or output_directory\n"
    "  2. Images and videos: $SCRAN_OUTPUT_DIR\n"
    "  3. Images only: $XDG_SCREENSHOTS_DIR (absolute)\n"
    "  4. Images: <XDG Pictures>/Screenshots/\n"
    "     Videos: <XDG Videos>/Screencasts/\n"
    "  5. Images: $HOME/Pictures/Screenshots/\n"
    "     Videos: $HOME/Videos/Screencasts/\n"
    "  Created if needed, except -d must exist. XDG paths come from user-dirs.dirs.\n"
    "\n"
    "Signals\n"
    "  Send SIGUSR1 to the running scran to start grabbing inputs again after releasing with <Tab>.\n"
    "  - Example:            `pkill -SIGUSR1 scran`\n"
    "  - As sway keybinding: `bindsym Shift+Alt+Tab exec 'pkill -SIGUSR1 scran'`\n"
    "\n"
    SCRAN_VERSION_STRING"\n"
;

bool
scran_handle_args(int argc, char *const *argv)
{
    struct scran_options *options = &g_state.options;

    char *opt_filename         = NULL;
    char *opt_output_directory = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "f:d:peACcBzsg:NvhU")) != -1) {
        switch (opt) {
        case 'f': opt_filename                                          = optarg; break;
        case 'd': opt_output_directory                                  = optarg; break;
        case 'p': g_state.seat.pointer_ctx.use_presses_only             = true;   break;
        case 'e': options->capture_and_exit_after_selection_init = true;   break;
        case 'A': options->disable_audio_capture                 = true;   break;
        case 'B': options->no_keepalive                          = true;   break;
        case 'z': options->freezeframe_at_startup                = true;   break;
        case 's': options->produce_slurp                         = true;   break;
        case 'U': options->hide_ui_level                         += 1;     break;
        case 'C': options->cursor_capture                        = SCRAN_OPT_CAPTURE_NO_CURSORS;  break;
        case 'c': options->cursor_capture                        = SCRAN_OPT_CAPTURE_ALL_CURSORS; break;
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
                        &options->custom_initial_selection_global_coordinates
                    )
                ) {
                    eprintf("Error: Failed to parse geometry string.\n");
                    return false;
                }
                options->have_custom_initial_selection = true;
            }
            break;
        case 'N': options->no_notifications                      = true;   break;
        case 'v':
            printf("%s\n", SCRAN_VERSION_STRING);
            exit(EXIT_SUCCESS);
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

    const char *supplied_output_dir = NULL;
    bool supplied_output_dir_should_mkdir = false;

    if (opt_output_directory && arg_output_directory) {
        eprintf("Error: Received both `-d` and `output_path`\n");
        return false;
    } else if (opt_output_directory) {
        supplied_output_dir = opt_output_directory;
    } else if (arg_output_directory) {
        if (!strcmp(arg_output_directory, "-")) {
            options->output_to_stdout = true;
        } else {
            supplied_output_dir = arg_output_directory;
            supplied_output_dir_should_mkdir = true;
        }
    } else {
        const char *env_output_directory = getenv("SCRAN_OUTPUT_DIR");
        if (env_output_directory && env_output_directory[0]) {
            supplied_output_dir = env_output_directory;
            supplied_output_dir_should_mkdir = true;
        }
    }

    if (!options->output_to_stdout) {
        if (supplied_output_dir) {
            struct scran_write_path *image_path = &options->image_path;

            image_path->should_mkdir = supplied_output_dir_should_mkdir;
            if (!set_output_directory(image_path, supplied_output_dir, NULL, 0)) {
                return false;
            }
            if (!image_path->should_mkdir && !ensure_directory_exists(image_path)) {
                return false;
            }

            options->video_path = *image_path;
        } else {
            options->image_path.should_mkdir = true;
            options->video_path.should_mkdir = true;
            if (!set_default_image_output_directory(&options->image_path)) {
                return false;
            }
            if (!set_default_video_output_directory(&options->video_path)) {
                return false;
            }
        }

        assert(!strcmp(options->filename_format, SCRAN_OUTPUT_FILENAME_FORMATSTRING_DEFAULT));
        if (opt_filename) {
            if (!handle_cli_arg_filename(options, opt_filename)) {
                return false;
            }
        }
    }

    return true;
}
