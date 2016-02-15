#ifndef _UTILS_H_
#define _UTILS_H_

#include <math.h>
#include <stdint.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define NUMOF(l) (sizeof(l) / sizeof(*l))

#define linear_to_dB(x) (log10(x) * 20)

/* Calculate the little string containing a bargraph
 * 'VU-meter' from the peak value measured
 */
const char* level(int channel, int* peak);

#endif

