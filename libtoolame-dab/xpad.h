#ifndef _XPAD_H_
#define _XPAD_H_

#include <stdint.h>

/* Initialise the xpad reader
 *
 * pad_fifo is the filename of the FIFO that will be created, and
 * can be used with mot-encoder.
 *
 * pad_len is the XPAD length, that also has to be given
 * to mot-encoder.
 *
 * returns 0  on success
 *         -1 on failure
 */
int xpad_init(char* pad_fifo, int pad_len);

/* Get len bytes of x-pad data, write into buf
 * returns either
 * - len if the read was sucessful
 * - 0   if there was no data
 * - -1  if there was an error (errno will be set)
 */
int xpad_read_len(uint8_t* buf, int len);

#endif

