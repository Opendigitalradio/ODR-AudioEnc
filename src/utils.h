#ifndef UTILS_H_
#define UTILS_H_

#include <math.h>
#include <stdint.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define NUMOF(l) (sizeof(l) / sizeof(*l))

#define linear_to_dB(x) (log10(x) * 20)

/* Calculate the little string containing a bargraph
 * 'VU-meter' from the peak value measured
 */
const char* level(int channel, int peak);

/* This defines the on-wire representation of a ZMQ message header.
 *
 * The data follows right after this header */
struct zmq_frame_header_t
{
    uint16_t version; // we support version=1 now
    uint16_t encoder; // see ZMQ_ENCODER_XYZ

    /* length of the 'data' field */
    uint32_t datasize;

    /* Audio level, peak, linear PCM */
    int16_t audiolevel_left;
    int16_t audiolevel_right;

    /* Data follows this header */
} __attribute__ ((packed));

#define ZMQ_ENCODER_FDK 1
#define ZMQ_ENCODER_TOOLAME 2

#define ZMQ_HEADER_SIZE sizeof(struct zmq_frame_header_t)

/* The expected frame size incl data of the given frame */
#define ZMQ_FRAME_SIZE(f) (sizeof(struct zmq_frame_header_t) + f->datasize)

#define ZMQ_FRAME_DATA(f) ( ((uint8_t*)f)+sizeof(struct zmq_frame_header_t) )


#endif

