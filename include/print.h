#ifndef PRINT_H
#define PRINT_H


#ifdef _DISABLE_eprintf
#define eprintf(fmt, ...) ((void)0)
#else
#define eprintf(fmt, ...) (void)fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

#ifdef NDEBUG
#define _DEBUG(fmt, ...) ((void)0)
#else
#define _DEBUG(fmt, ...) eprintf(fmt, ##__VA_ARGS__)
#define DEBUG(fmt, ...) _DEBUG("DEBUG: " fmt, ##__VA_ARGS__)
#endif


#endif
