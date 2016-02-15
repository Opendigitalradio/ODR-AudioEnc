#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>

#include "xpad.h"

static int xpad_fd = 0;

/* The F-PAD has to be:
    uint16_t fpad = 0x2; // CI flag

    if (xpad_len()) {
        fpad |= 1<<13; // variable length X-PAD
    }

   which is included by mot-encoder in the file/fifo
   it generates
   */

/* Create and open the desired PAD input fifo
 */
int xpad_init(char* pad_fifo, int pad_len)
{
    if (mkfifo(pad_fifo, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0) {
        if (errno != EEXIST) {
            fprintf(stderr, "Can't create pad file: %d!\n", errno);
            return -1;
        }
    }

    xpad_fd = open(pad_fifo, O_RDONLY | O_NONBLOCK);
    if (xpad_fd == -1) {
        fprintf(stderr, "Can't open pad file!\n");
        return -1;
    }

    int flags = fcntl(xpad_fd, F_GETFL, 0);
    if (fcntl(xpad_fd, F_SETFL, flags | O_NONBLOCK)) {
        fprintf(stderr, "Can't set non-blocking mode in pad file!\n");
        return -1;
    }

    return 0;
}

int xpad_read_len(uint8_t* buf, int len)
{
    if (xpad_fd == 0) return 0;

    ssize_t num_read = 0;

    while (num_read < len) {
        ssize_t r = read(xpad_fd, buf + num_read, len - num_read);

        if(r < 0) {
            if (errno == EAGAIN) {
                return 0;
            }
            else {
                perror("PAD input read error");
                return -1;
            }
        }
        else if (r == 0) {
            // reached end of data
            return 0;
        }

        num_read += r;
    }

#if XPAD_DEBUG
    int i;
    for (i = 0; i < len; i++) {
        fprintf(stderr, "%02x ", buf[i]);
    }
    fprintf(stderr, "\n");
#endif

    return num_read;
}

