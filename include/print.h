#ifndef SCRAN_PRINT_H
#define SCRAN_PRINT_H


#include <stdio.h>


#ifdef SCRAN_DISABLE_eprintf
#define eprintf(fmt, ...) ((void)0)
#else
#define eprintf(fmt, ...) (void)fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

#ifdef NDEBUG
#define DEBUG_(fmt, ...) ((void)0)
#else
#define DEBUG_(fmt, ...) eprintf(fmt, ##__VA_ARGS__)
#endif
#define DEBUG(fmt, ...) DEBUG_("DEBUG: " fmt, ##__VA_ARGS__)

#define DEBUG_BLBOXI(box) DEBUG("x0=%d, y0=%d, x1=%d, y1=%d\n", box.x0, box.y0, box.x1, box.y1)
#define DEBUG_BLRECTI(rect) DEBUG("x=%d, y=%d, w=%d, h=%d\n", rect.x, rect.y, rect.w, rect.h)
#define DEBUG_POINT(x, y) DEBUG("x=%d, y=%d\n", (x), (y))


#endif
