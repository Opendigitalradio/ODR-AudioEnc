/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2013,2014 Matthias P. Braendli
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

#include "AlsaInput.h"
#include "SampleQueue.h"
#include "zmq.hpp"

extern "C" {
#include "encryption.h"
}

#include <string>
#include <getopt.h>
#include <cstdio>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "libAACenc/include/aacenc_lib.h"

extern "C" {
#include <fec.h>
}

using namespace std;

void usage(const char* name) {
    fprintf(stderr,
    "dabplus-enc-alsa-zmq %s is a HE-AACv2 encoder for DAB+\n"
    "based on fdk-aac-dabplus that can read from a ALSA source\n"
    "and encode to a ZeroMQ output for ODR-DabMux.\n"
    "\n"
    "The -D option enables experimental sound card clock drift compensation.\n"
    "A consumer sound card has a clock that is always a bit imprecise, and\n"
    "would drift off after some time. ODR-DabMux cannot handle such drift\n"
    "because it would have to throw away or insert a full DAB+ superframe,\n"
    "which would create audible artifacts. This drift compensation can\n"
    "make sure that the encoding rate is correct by inserting or deleting\n"
    "audio samples.\n"
    "\n"
    "When this option is enabled, you will see U and O<number> printed in\n"
    "the console. These correspond to audio underruns and overruns caused\n"
    "by sound card clock drift. When sparse, they should not create audible\n"
    "artifacts.\n"
    "\n"
    "This encoder includes PAD (DLS and MOT Slideshow) support by\n"
    "http://rd.csp.it to be used with mot-encoder\n"
    "\n"
    "  http://opendigitalradio.org\n"
    "\nUsage:\n"
    "%s [OPTION...]\n",
#if defined(GITVERSION)
    GITVERSION
#else
    PACKAGE_VERSION
#endif
    , name);
    fprintf(stderr,
    "     -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.\n"
    "     -D, --drift-comp                     Enable ALSA sound card drift compensation.\n"
    //"   -i, --input=FILENAME                 Input filename (default: stdin).\n"
    "     -o, --output=URI                     Output zmq uri. (e.g. 'tcp://*:9000')\n"
    "     -a, --afterburner                    Turn on AAC encoder quality increaser.\n"
    "     -p, --pad=BYTES                      Set PAD size in bytes.\n"
    "     -P, --pad-fifo=FILENAME              Set PAD data input fifo name (default: /tmp/pad.fifo).\n"
    "     -d, --device=alsa_device             Set ALSA input device (default: \"default\").\n"
    "     -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).\n"
    "     -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).\n"
    "     -k, --secret-key=FILE                Set the secret key for encryption.\n"
    //"   -V, --version                        Print version and exit.\n"
    "\n"
    "Only the tcp:// zeromq transport has been tested until now.\n"
    );

}

int prepare_aac_encoder(
        HANDLE_AACENCODER *encoder,
        int subchannel_index,
        int channels,
        int sample_rate,
        int afterburner)
{
    HANDLE_AACENCODER handle = *encoder;

    int aot = AOT_DABPLUS_AAC_LC;

    CHANNEL_MODE mode;
    switch (channels) {
        case 1: mode = MODE_1; break;
        case 2: mode = MODE_2; break;
        default:
                fprintf(stderr, "Unsupported channels number %d\n", channels);
                return 1;
    }


    if (aacEncOpen(&handle, 0x01|0x02|0x04, channels) != AACENC_OK) {
        fprintf(stderr, "Unable to open encoder\n");
        return 1;
    }

    *encoder = handle;

    if(channels == 2 && subchannel_index <= 6)
        aot = AOT_DABPLUS_PS;
    else if((channels == 1 && subchannel_index <= 8) || subchannel_index <= 10)
        aot = AOT_DABPLUS_SBR;

    fprintf(stderr, "Using %d subchannels. AAC type: %s%s%s. channels=%d, sample_rate=%d\n",
            subchannel_index,
            aot == AOT_DABPLUS_PS ? "HE-AAC v2" : "",
            aot == AOT_DABPLUS_SBR ? "HE-AAC" : "",
            aot == AOT_DABPLUS_AAC_LC ? "AAC-LC" : "",
            channels, sample_rate);

    if (aacEncoder_SetParam(handle, AACENC_AOT, aot) != AACENC_OK) {
        fprintf(stderr, "Unable to set the AOT\n");
        return 1;
    }
    if (aacEncoder_SetParam(handle, AACENC_SAMPLERATE, sample_rate) != AACENC_OK) {
        fprintf(stderr, "Unable to set the sample rate\n");
        return 1;
    }
    if (aacEncoder_SetParam(handle, AACENC_CHANNELMODE, mode) != AACENC_OK) {
        fprintf(stderr, "Unable to set the channel mode\n");
        return 1;
    }
    if (aacEncoder_SetParam(handle, AACENC_CHANNELORDER, 1) != AACENC_OK) {
        fprintf(stderr, "Unable to set the wav channel order\n");
        return 1;
    }
    if (aacEncoder_SetParam(handle, AACENC_GRANULE_LENGTH, 960) != AACENC_OK) {
        fprintf(stderr, "Unable to set the granule length\n");
        return 1;
    }
    if (aacEncoder_SetParam(handle, AACENC_TRANSMUX, TT_DABPLUS) != AACENC_OK) {
        fprintf(stderr, "Unable to set the RAW transmux\n");
        return 1;
    }

    /*if (aacEncoder_SetParam(handle, AACENC_BITRATEMODE, 7 *AACENC_BR_MODE_SFR*)
     * != AACENC_OK) {
        fprintf(stderr, "Unable to set the bitrate mode\n");
        return 1;
    }*/


    fprintf(stderr, "AAC bitrate set to: %d\n", subchannel_index*8000);
    if (aacEncoder_SetParam(handle, AACENC_BITRATE, subchannel_index*8000) != AACENC_OK) {
        fprintf(stderr, "Unable to set the bitrate\n");
        return 1;
    }
    if (aacEncoder_SetParam(handle, AACENC_AFTERBURNER, afterburner) != AACENC_OK) {
        fprintf(stderr, "Unable to set the afterburner mode\n");
        return 1;
    }
    if (aacEncEncode(handle, NULL, NULL, NULL, NULL) != AACENC_OK) {
        fprintf(stderr, "Unable to initialize the encoder\n");
        return 1;
    }
    return 0;
}

/* Get the number of columns of the terminal this runs in
 */
int get_win_columns()
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        return 0;
    }

    return w.ws_col;
}

#define WINDOW_MARGIN_RIGHT 6

/* Add line breaks in regular intervals to the
 * little sequence of dots because my terminal
 */
void print_status(const char* status, int *consumed_cols)
{
    fprintf(stdout, "%s", status);
    *consumed_cols -= strnlen(status, *consumed_cols);

    if (*consumed_cols <= 0) {
        fprintf(stdout, "\n");
        *consumed_cols = get_win_columns();

        // Guarantee that it's never negative
        if (*consumed_cols > WINDOW_MARGIN_RIGHT)
            *consumed_cols -= WINDOW_MARGIN_RIGHT;
    }
}

#define no_argument 0
#define required_argument 1
#define optional_argument 2

int main(int argc, char *argv[])
{
    int subchannel_index = 8; //64kbps subchannel
    int ch=0;
    const char *alsa_device = "default";
    const char *outuri = NULL;
    int sample_rate=48000, channels=2;
    const int bytes_per_sample = 2;
    void *rs_handler = NULL;
    bool afterburner = false;
    bool drift_compensation = false;
    AACENC_InfoStruct info = { 0 };

    const char* pad_fifo = "/tmp/pad.fifo";
    int pad_fd;
    unsigned char pad_buf[128];
    int padlen;


    /* Data for ZMQ CURVE authentication */
    char* keyfile = NULL;
    char secretkey[CURVE_KEYLEN+1];

    const struct option longopts[] = {
        {"bitrate",     required_argument,  0, 'b'},
        {"output",      required_argument,  0, 'o'},
        {"device",      required_argument,  0, 'd'},
        {"rate",        required_argument,  0, 'r'},
        {"channels",    required_argument,  0, 'c'},
        {"pad",         required_argument,  0, 'p'},
        {"pad-fifo",    required_argument,  0, 'P'},
        {"secret-key",  required_argument,  0, 'k'},
        {"drift-comp",  no_argument,        0, 'D'},
        {"afterburner", no_argument,        0, 'a'},
        {"help",        no_argument,        0, 'h'},
        {0,0,0,0},
    };

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "hab:c:k:o:r:d:Dp:P:", longopts, &index);
        switch (ch) {
        case 'd':
            alsa_device = optarg;
            break;
        case 'a':
            afterburner = true;
            break;
        case 'b':
            subchannel_index = atoi(optarg) / 8;
            break;
        case 'c':
            channels = atoi(optarg);
            break;
        case 'r':
            sample_rate = atoi(optarg);
            break;
        case 'o':
            outuri = optarg;
            break;
        case 'k':
            keyfile = optarg;
            break;
        case 'D':
            drift_compensation = true;
            break;
        case 'p':
            padlen = atoi(optarg);
            break;
        case 'P':
            pad_fifo = optarg;
            break;
        case '?':
        case 'h':
            usage(argv[0]);
            return 1;
        }
    }

    if(subchannel_index < 1 || subchannel_index > 24) {
        fprintf(stderr, "Bad subchannels number: %d, try other bitrate.\n",
                subchannel_index);
        return 1;
    }

    if ( ! (sample_rate == 32000 || sample_rate == 48000)) {
        fprintf(stderr, "Invalid sample rate. Possible values are: 32000, 48000.\n");
        return 1;
    }

    const int enc_calls_per_output = sample_rate / 16000;

    if (!outuri) {
        fprintf(stderr, "ZeroMQ output URI not defined\n");
        return 1;
    }

    if (padlen != 0) {
        int flags;
        if (mkfifo(pad_fifo, S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0) {
            if (errno != EEXIST) {
                fprintf(stderr, "Can't create pad file: %d!\n", errno);
                return 1;
            }
        }
        pad_fd = open(pad_fifo, O_RDONLY | O_NONBLOCK);
        if (pad_fd == -1) {
            fprintf(stderr, "Can't open pad file!\n");
            return 1;
        }
        flags = fcntl(pad_fd, F_GETFL, 0);
        if (fcntl(pad_fd, F_SETFL, flags | O_NONBLOCK)) {
            fprintf(stderr, "Can't set non-blocking mode in pad file!\n");
            return 1;
        }
    }

    zmq::context_t zmq_ctx;
    zmq::socket_t zmq_sock(zmq_ctx, ZMQ_PUB);

    if (keyfile) {
        fprintf(stderr, "Enabling encryption\n");

        int rc = readkey(keyfile, secretkey);
        if (rc) {
            fprintf(stderr, "Error reading secret key\n");
            return 2;
        }

        const int yes = 1;
        zmq_sock.setsockopt(ZMQ_CURVE_SERVER,
                &yes, sizeof(yes));

        zmq_sock.setsockopt(ZMQ_CURVE_SECRETKEY,
                secretkey, CURVE_KEYLEN);
    }
    zmq_sock.connect(outuri);

    HANDLE_AACENCODER encoder;

    if (prepare_aac_encoder(&encoder, subchannel_index, channels,
                sample_rate, afterburner) != 0) {
        fprintf(stderr, "Encoder preparation failed\n");
        return 2;
    }

    if (aacEncInfo(encoder, &info) != AACENC_OK) {
        fprintf(stderr, "Unable to get the encoder info\n");
        return 1;
    }

    // Each DAB+ frame will need input_size audio bytes
    const int input_size = channels * bytes_per_sample * info.frameLength;
    fprintf(stderr, "DAB+ Encoding: framelen=%d (%dB)\n",
            info.frameLength,
            input_size);

    uint8_t input_buf[input_size];

    int max_size = 2*input_size + NUM_SAMPLES_PER_CALL;

    SampleQueue<uint8_t> queue(BYTES_PER_SAMPLE, channels, max_size);

    /* symsize=8, gfpoly=0x11d, fcr=0, prim=1, nroots=10, pad=135 */
    rs_handler = init_rs_char(8, 0x11d, 0, 1, 10, 135);
    if (rs_handler == NULL) {
        perror("init_rs_char failed");
        return 1;
    }

    // We'll use either of the two possible alsa inputs.
    AlsaInputThreaded alsa_in_threaded(alsa_device, channels, sample_rate, queue);
    AlsaInputDirect alsa_in_direct(alsa_device, channels, sample_rate);

    if (drift_compensation) {
        if (alsa_in_threaded.prepare() != 0) {
            fprintf(stderr, "Alsa preparation failed\n");
            return 1;
        }

        fprintf(stderr, "Start ALSA capture thread\n");
        alsa_in_threaded.start();
    }
    else {
        if (alsa_in_direct.prepare() != 0) {
            fprintf(stderr, "Alsa preparation failed\n");
            return 1;
        }
    }

    int outbuf_size = subchannel_index*120;
    uint8_t outbuf[20480];

    if(outbuf_size % 5 != 0) {
        fprintf(stderr, "(outbuf_size mod 5) = %d\n", outbuf_size % 5);
    }

    fprintf(stderr, "Starting encoding\n");

    int remaining_line_len = get_win_columns() - 6;

    int send_error_count = 0;
    struct timespec tp_next;
    clock_gettime(CLOCK_MONOTONIC, &tp_next);

    int calls = 0; // for checking
    while (1) {
        int in_identifier[] = {IN_AUDIO_DATA, IN_ANCILLRY_DATA};
        int out_identifier = OUT_BITSTREAM_DATA;

        AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
        AACENC_InArgs in_args = { 0 };
        AACENC_OutArgs out_args = { 0 };
        void *in_ptr[2], *out_ptr;
        int in_size[2], in_elem_size[2];
        int out_size, out_elem_size;


        // -------------- wait the right amount of time
        if (drift_compensation) {
            struct timespec tp_now;
            clock_gettime(CLOCK_MONOTONIC, &tp_now);

            unsigned long time_now  = (1000000000ul * tp_now.tv_sec) +
                tp_now.tv_nsec;
            unsigned long time_next = (1000000000ul * tp_next.tv_sec) +
                tp_next.tv_nsec;

            const unsigned long dabplus_superframe_nsec = 120000000ul;

            const unsigned long wait_time =
                dabplus_superframe_nsec / enc_calls_per_output;

            unsigned long waiting = wait_time - (time_now - time_next);
            if ((time_now - time_next) < wait_time) {
                //printf("Sleep %zuus\n", waiting / 1000);
                usleep(waiting / 1000);
            }

            // Move our time_counter 60ms into the future.
            // The encoder needs two calls for one frame
            tp_next.tv_nsec += wait_time;
            if (tp_next.tv_nsec >  1000000000L) {
                tp_next.tv_nsec -= 1000000000L;
                tp_next.tv_sec  += 1;
            }
        }

        // --------------- Read data from the PAD fifo
        int ret;
        if (padlen != 0) {
            ret = read(pad_fd, pad_buf, padlen);
        }
        else {
            ret = 0;
        }


        if(ret < 0 && errno == EAGAIN) {
            // If this condition passes, there is no data to be read
            in_buf.numBufs = 1;    // Samples;
        }
        else if(ret >= 0) {
            // Otherwise, you're good to go and buffer should contain "count" bytes.
            in_buf.numBufs = 2;    // Samples + Data;
            if (ret > 0)
                print_status("p", &remaining_line_len);
        }
        else {
            // Some other error occurred during read.
            fprintf(stderr, "Unable to read from PAD!\n");
                        break;
        }

        // -------------- Read Data
        memset(outbuf, 0x00, outbuf_size);

        ssize_t read;
        if (drift_compensation) {
            if (alsa_in_threaded.fault_detected()) {
                fprintf(stderr, "Detected fault in alsa input!\n");
                break;
            }

            size_t overruns;
            read = queue.pop(input_buf, input_size, &overruns); // returns bytes

            if (read != input_size) {
                print_status("U", &remaining_line_len);
            }

            if (overruns) {
                char status[16];
                snprintf(status, 16, "O%zu", overruns);
                print_status(status, &remaining_line_len);
            }
        }
        else {
            read = alsa_in_direct.read(input_buf, input_size);
            if (read < 0) {
                break;
            }
            else if (read != input_size) {
                fprintf(stderr, "Short alsa read !\n");
            }
        }

        // -------------- AAC Encoding

        in_ptr[0] = input_buf;
        in_ptr[1] = pad_buf;
        in_size[0] = read;
        in_size[1] = padlen;
        in_elem_size[0] = BYTES_PER_SAMPLE;
        in_elem_size[1] = sizeof(uint8_t);
        in_args.numInSamples = input_size/BYTES_PER_SAMPLE;
        in_args.numAncBytes = padlen;

        in_buf.bufs = (void**)&in_ptr;
        in_buf.bufferIdentifiers = in_identifier;
        in_buf.bufSizes = in_size;
        in_buf.bufElSizes = in_elem_size;

        out_ptr = outbuf;
        out_size = sizeof(outbuf);
        out_elem_size = 1;
        out_buf.numBufs = 1;
        out_buf.bufs = &out_ptr;
        out_buf.bufferIdentifiers = &out_identifier;
        out_buf.bufSizes = &out_size;
        out_buf.bufElSizes = &out_elem_size;

        AACENC_ERROR err;
        if ((err = aacEncEncode(encoder, &in_buf, &out_buf, &in_args, &out_args))
                != AACENC_OK) {
            if (err == AACENC_ENCODE_EOF) {
                fprintf(stderr, "encoder error: EOF reached\n");
                break;
            }
            fprintf(stderr, "Encoding failed (%d)\n", err);
            break;
        }
        calls++;

        /* Check if the encoder has generated output data */
        if (out_args.numOutBytes != 0)
        {
            // Our timing code depends on this
            if (! ((sample_rate == 32000 && calls == 2) ||
                   (sample_rate == 48000 && calls == 3)) ) {
                fprintf(stderr, "INTERNAL ERROR! sample rate %d, calls %d\n",
                    sample_rate, calls);
                }
            calls = 0;

            // ----------- RS encoding
            int row, col;
            unsigned char buf_to_rs_enc[110];
            unsigned char rs_enc[10];
            for(row=0; row < subchannel_index; row++) {
                for(col=0;col < 110; col++) {
                    buf_to_rs_enc[col] = outbuf[subchannel_index * col + row];
                }

                encode_rs_char(rs_handler, buf_to_rs_enc, rs_enc);

                for(col=110; col<120; col++) {
                    outbuf[subchannel_index * col + row] = rs_enc[col-110];
                    assert(subchannel_index * col + row < outbuf_size);
                }
            }

            // ------------ ZeroMQ transmit
            try {
                zmq_sock.send(outbuf, outbuf_size, ZMQ_DONTWAIT);
            }
            catch (zmq::error_t& e) {
                fprintf(stderr, "ZeroMQ send error !\n");
                send_error_count ++;
            }

            if (send_error_count > 10)
            {
                fprintf(stderr, "ZeroMQ send failed ten times, aborting!\n");
                break;
            }

            if (out_args.numOutBytes + row*10 == outbuf_size)
                print_status(".", &remaining_line_len);
        }

        fflush(stdout);
    }

    zmq_sock.close();
    free_rs_char(rs_handler);

    aacEncClose(&encoder);

}

