#include <fcntl.h>

#include <sys/mman.h>

#include "init.h"

// TODO: We only use this in one place. Just define it where we use it and remove this
// file.


#define SHM_TMP_FILENAME "/icicle-wayland-client-jfkdsalfj"

// Open shm file, get fd, unlink file, return fd.
// The underlying file survives unlinking.
int
shm_open_anon(void)
{
    // TODO: Generate random filenames in case file already exists?
    int fd = shm_open(SHM_TMP_FILENAME, O_CREAT | O_RDWR | O_EXCL, 0600);

    if (fd >= 0) {
        shm_unlink(SHM_TMP_FILENAME);
    }

    return fd;
}

