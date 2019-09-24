#pragma once

#include <math.h>
#include <stdint.h>
#include <stddef.h>

#define NUMOF(l) (sizeof(l) / sizeof(*l))

#define linear_to_dB(x) (log10(x) * 20)

/*! Calculate the little string containing a bargraph
 * 'VU-meter' from the peak value measured
 */
const char* level(int channel, int peak);

size_t strlen_utf8(const char *s);

