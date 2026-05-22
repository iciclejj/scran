#ifndef SCRAN_UTIL_H
#define SCRAN_UTIL_H


#include <stdbool.h>
#include <stddef.h>


bool scran_full_write(int fd, const char *src, size_t n_bytes);


#endif
