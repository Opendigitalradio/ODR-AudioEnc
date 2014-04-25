#ifndef UTILS_H_
#define UTILS_H_

#include <math.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define NUMOF(l) (sizeof(l) / sizeof(*l))

#define linear_to_dB(x) (log10(x) * 20)


const char* level(int channel, int* peak);

#endif

