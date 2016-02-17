#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "mem.h"
#include "bitstream.h"

/*****************************************************************************
 *
 *  bit_stream.c package
 *  Author:  Jean-Georges Fritsch, C-Cube Microsystems
 *           Matthias P. Braendli, www.opendigitalradio.org
 *  Changes
 *       Apr 2000 - removed all the file input routines. MFC
 *       Feb 2016 - removed all sort of things to make Toolame a library. mpb
 *****************************************************************************/

/********************************************************************
  This package provides functions to write (exclusive or read)
  information from (exclusive or to) the bit stream.

  If the bit stream is opened in read mode only the get functions are
  available. If the bit stream is opened in write mode only the put
  functions are available.
 ********************************************************************/

/*open_bit_stream_w(); open the device to write the bit stream into it    */
/*close_bit_stream();  close the device containing the bit stream         */
/*alloc_buffer();      open and initialize the buffer;                    */
/*desalloc_buffer();   empty and close the buffer                         */
/*put1bit(); write 1 bit from the bit stream  */
/*put1bit(); write 1 bit from the bit stream  */
/*putbits(); write N bits from the bit stream */

/* You must have one frame in memory if you are in DAB mode                 */
/* in conformity of the norme ETS 300 401 http://www.etsi.org               */
/* see toollame.c                                                           */
int minimum = MINIMUM;

/* empty the buffer to the output device when the buffer becomes full */
void empty_buffer (Bit_stream_struc * bs, int minimum)
{
    int j = 0;
    if (bs->output_buffer_written != 0) {
        fprintf(stderr, "ERROR: libtoolame output buffer was not emptied\n");
    }

    for (int i = bs->buf_size - 1; i >= minimum; i--) {
        if (j >= bs->output_buffer_size) {
            fprintf(stderr, "ERROR: libtoolame output buffer too small (%d vs %d)!\n",
                    bs->output_buffer_size, bs->buf_size - minimum);
            break;
        }

        bs->output_buffer[j] = bs->buf[i];
        j++;
    }
    bs->output_buffer_written = j;

    for (int i = minimum - 1; i >= 0; i--) {
        bs->buf[bs->buf_size - minimum + i] = bs->buf[i];
    }

    bs->buf_byte_idx = bs->buf_size - 1 - minimum;
    bs->buf_bit_idx = 8;
}


/* open the device to write the bit stream into it */
void open_bit_stream_w (Bit_stream_struc * bs, int size)
{
    alloc_buffer (bs, size);
    bs->buf_byte_idx = size - 1;
    bs->buf_bit_idx = 8;
    bs->totbit = 0;
    bs->mode = WRITE_MODE;
    bs->eob = FALSE;
    bs->eobs = FALSE;
}

/*close the device containing the bit stream after a write process*/
void close_bit_stream_w (Bit_stream_struc * bs)
{
    putbits (bs, 0, 7);
    empty_buffer (bs, bs->buf_byte_idx + 1);
    desalloc_buffer (bs);
}

/*open and initialize the buffer; */
void alloc_buffer (Bit_stream_struc * bs, int size)
{
    bs->buf =
        (unsigned char *) mem_alloc (size * sizeof (unsigned char), "buffer");
    bs->buf_size = size;
}

/*empty and close the buffer */
void desalloc_buffer (Bit_stream_struc * bs)
{
    free (bs->buf);
}

const int putmask[9] = { 0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff };

/*write 1 bit from the bit stream */
void put1bit (Bit_stream_struc * bs, int bit)
{
    bs->totbit++;

    bs->buf[bs->buf_byte_idx] |= (bit & 0x1) << (bs->buf_bit_idx - 1);
    bs->buf_bit_idx--;
    if (!bs->buf_bit_idx) {
        bs->buf_bit_idx = 8;
        bs->buf_byte_idx--;
        if (bs->buf_byte_idx < 0)
            empty_buffer (bs, minimum);
        bs->buf[bs->buf_byte_idx] = 0;
    }
}

/*write N bits into the bit stream */
void putbits (Bit_stream_struc * bs, unsigned int val, int N)
{
    register int j = N;
    register int k, tmp;

    /* if (N > MAX_LENGTH)
       fprintf(stderr, "Cannot read or write more than %d bits at a time.\n", MAX_LENGTH); ignore check!! MFC Apr 00 */

    bs->totbit += N;
    while (j > 0) {
        k = MIN (j, bs->buf_bit_idx);
        tmp = val >> (j - k);
        bs->buf[bs->buf_byte_idx] |= (tmp & putmask[k]) << (bs->buf_bit_idx - k);
        bs->buf_bit_idx -= k;
        if (!bs->buf_bit_idx) {
            bs->buf_bit_idx = 8;
            bs->buf_byte_idx--;
            if (bs->buf_byte_idx < 0)
                empty_buffer (bs, minimum);
            bs->buf[bs->buf_byte_idx] = 0;
        }
        j -= k;
    }
}

