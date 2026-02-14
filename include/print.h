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
#endif
#define DEBUG(fmt, ...) _DEBUG("DEBUG: " fmt, ##__VA_ARGS__)

#define DEBUG_BLBOXI(box) DEBUG("x0=%d, y0=%d, x1=%d, y1=%d\n", box.x0, box.y0, box.x1, box.y1)


#endif
