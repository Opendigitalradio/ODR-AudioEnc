#include "utils.h"
#include <unistd.h>
#include <stdint.h>
#include <math.h>

/* Taken from sox */
const char* level(int channel, int* peak)
{
    static char const * const text[][2] = {
        /* White: 2dB steps */
        {"", ""}, {"-", "-"}, {"=", "="}, {"-=", "=-"},
        {"==", "=="}, {"-==", "==-"}, {"===", "==="}, {"-===", "===-"},
        {"====", "===="}, {"-====", "====-"}, {"=====", "====="},
        {"-=====", "=====-"}, {"======", "======"},
        /* Red: 1dB steps */
        {"!=====", "=====!"},
    };
    int const red = 1, white = NUMOF(text) - red;

    double linear = (double)(*peak) / INT16_MAX;

    int vu_dB = linear ? floor(2 * white + red + linear_to_dB(linear)) : 0;

    int index = vu_dB < 2 * white ?
        MAX(vu_dB / 2, 0) :
        MIN(vu_dB - white, red + white - 1);

    *peak = 0;

    return text[index][channel];

}

