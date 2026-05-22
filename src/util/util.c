#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include "util/util.h"

bool scran_full_write(int fd, const char *src, size_t n_bytes) {
    const char *src_curr = src;

    while (n_bytes > 0) {
        ssize_t bytes_written = write(fd, src_curr, n_bytes);
        if (bytes_written == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes_written == 0) {
            return false;
        }

        src_curr += bytes_written;
        n_bytes -= bytes_written;
    }

    return true;
}
