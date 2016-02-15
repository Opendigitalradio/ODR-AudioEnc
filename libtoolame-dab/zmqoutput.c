#include "zmqoutput.h"
#include <zmq.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static void *zmq_context;

// Buffer containing at maximum one frame
unsigned char* zmqbuf;

// The current data length (smaller than allocated
// buffer size)
size_t zmqbuf_len;

static int zmq_peak_left = 0;
static int zmq_peak_right = 0;

void zmqoutput_set_peaks(int left, int right)
{
    zmq_peak_left = left;
    zmq_peak_right = right;
}

int zmqoutput_open(Bit_stream_struc *bs, const char* uri_list)
{
    zmq_context = zmq_ctx_new();
    bs->zmq_sock = zmq_socket(zmq_context, ZMQ_PUB);
    if (bs->zmq_sock == NULL) {
        fprintf(stderr, "Error occurred during zmq_socket: %s\n",
                zmq_strerror(errno));
        return -1;
    }

    char* uris = strdup(uri_list);
    char* saveptr = NULL;

    for (; ; uris = NULL) {
        char* uri = strtok_r(uris, ";", &saveptr);


        if (uri) {
            fprintf(stderr, "Connecting ZMQ to %s\n", uri);
            if (zmq_connect(bs->zmq_sock, uri) != 0) {
                fprintf(stderr, "Error occurred during zmq_connect: %s\n",
                        zmq_strerror(errno));
                free(uris);
                return -1;
            }
        }
        else {
            break;
        }
    }

    free(uris);

    zmqbuf = (unsigned char*)malloc(bs->zmq_framesize);
    if (zmqbuf == NULL) {
        fprintf(stderr, "Unable to allocate ZMQ buffer\n");
        exit(0);
    }
    zmqbuf_len = 0;
    return 0;
}

int zmqoutput_write_byte(Bit_stream_struc *bs, unsigned char data)
{
    zmqbuf[zmqbuf_len++] = data;

    if (zmqbuf_len == bs->zmq_framesize) {

        int frame_length = sizeof(struct zmq_frame_header) + zmqbuf_len;

        struct zmq_frame_header* header =
            malloc(frame_length);

        uint8_t* txframe = ((uint8_t*)header) + sizeof(struct zmq_frame_header);

        header->version          = 1;
        header->encoder          = ZMQ_ENCODER_TOOLAME;
        header->datasize         = zmqbuf_len;
        header->audiolevel_left  = zmq_peak_left;
        header->audiolevel_right = zmq_peak_right;

        memcpy(txframe, zmqbuf, zmqbuf_len);

        int send_error = zmq_send(bs->zmq_sock, header, frame_length,
                ZMQ_DONTWAIT);

        free(header);
        header = NULL;

        if (send_error < 0) {
            fprintf(stderr, "ZeroMQ send failed! %s\n", zmq_strerror(errno));
        }

        zmqbuf_len = 0;

        return bs->zmq_framesize;
    }

    return 0;

}

void zmqoutput_close(Bit_stream_struc *bs)
{
    if (bs->zmq_sock)
        zmq_close(bs->zmq_sock);

    if (zmq_context)
        zmq_ctx_destroy(zmq_context);

    if (zmqbuf) {
        free(zmqbuf);
        zmqbuf = NULL;
    }
}

