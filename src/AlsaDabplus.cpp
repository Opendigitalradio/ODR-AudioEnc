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

#include <string>
#include <getopt.h>
#include <cstdio>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "libAACenc/include/aacenc_lib.h"

extern "C" {
#include <fec.h>
}

using namespace std;

void usage(const char* name) {
    fprintf(stderr, "%s [OPTION...]\n", name);
    fprintf(stderr,
"     -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.\n"
//"   -d, --data=FILENAME                  Set data filename.\n"
//"   -g, --fs-bug                         Turn on FS bug mitigation.\n"
//"   -i, --input=FILENAME                 Input filename (default: stdin).\n"
"     -o, --output=URI                     Output zmq uri. (e.g. 'tcp://*:9000')\n"
"     -a, --afterburner                    Turn on AAC encoder quality increaser.\n"
//"   -m, --message                        Turn on AAC frame messages.\n"
//"   -p, --pad=BYTES                      Set PAD size in bytes.\n"
//"   -f, --format={ wav, raw }            Set input file format (default: wav).\n"
"     -d, --device=alsa_device             Set ALSA input device (default: \"default\").\n"
"     -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).\n"
"     -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).\n"
//"   -t, --type=TYPE                      Set data type (dls|pad|packet|dg).\n"
//"   -v, --verbose=LEVEL                  Set verbosity level.\n"
//"   -V, --version                        Print version and exit.\n"
//"   --mi=[ 0, ... ]                      Set AAC frame messages interval in milliseconds.\n"
//"   --ma=[ 0, ... ]                      Set AAC frame messages attack time in milliseconds.\n"
//"   -l, --lp                             Set frame size to 1024 instead of 960.\n"
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


#define no_argument 0
#define required_argument 1
#define optional_argument 2

int main(int argc, char *argv[]) {
    int subchannel_index = 8; //64kbps subchannel
    int ch=0;
    const char *alsa_device = "default";
    const char *outuri = NULL;
    int sample_rate=48000, channels=2;
    const int bytes_per_sample = 2;
    void *rs_handler = NULL;
    int afterburner = 0;
    AACENC_InfoStruct info = { 0 };

    const struct option longopts[] = {
        {"bitrate",     required_argument,  0, 'b'},
        {"output",      required_argument,  0, 'o'},
        {"device",      required_argument,  0, 'd'},
        {"rate",        required_argument,  0, 'r'},
        {"channels",    required_argument,  0, 'c'},
        {"afterburner", no_argument,        0, 'a'},
        {"help",        no_argument,        0, 'h'},
        {0,0,0,0},
    };

    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "hab:c:o:r:d:", longopts, &index);
        switch (ch) {
        case 'd':
            alsa_device = optarg;
            break;
        case 'a':
            afterburner = 1;
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

    fprintf(stderr, "Setting up ZeroMQ socket\n");
    if (!outuri) {
        fprintf(stderr, "ZeroMQ output URI not defined\n");
        return 1;
    }

    zmq::context_t zmq_ctx;
    zmq::socket_t zmq_sock(zmq_ctx, ZMQ_PUB);
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

    AlsaInput alsa_in(alsa_device, channels, sample_rate, queue);

    if (alsa_in.prepare() != 0) {
        fprintf(stderr, "Alsa preparation failed\n");
        return 1;
    }

    fprintf(stderr, "Start ALSA capture thread\n");
    alsa_in.start();

    int outbuf_size = subchannel_index*120;
    uint8_t outbuf[20480];

    if(outbuf_size % 5 != 0) {
        fprintf(stderr, "(outbuf_size mod 5) = %d\n", outbuf_size % 5);
    }

    fprintf(stderr, "Starting encoding\n");

    int send_error_count = 0;
    struct timespec tp_next;
    clock_gettime(CLOCK_MONOTONIC, &tp_next);

    int calls = 0; // for checking
    while (1) {
        int in_identifier = IN_AUDIO_DATA;
        int out_identifier = OUT_BITSTREAM_DATA;

        AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
        AACENC_InArgs in_args = { 0 };
        AACENC_OutArgs out_args = { 0 };
        void *in_ptr, *out_ptr;
        int in_size, in_elem_size;
        int out_size, out_elem_size;


        // -------------- wait the right amount of time
        struct timespec tp_now;
        clock_gettime(CLOCK_MONOTONIC, &tp_now);

        unsigned long time_now  = (1000000000ul * tp_now.tv_sec) +
            tp_now.tv_nsec;
        unsigned long time_next = (1000000000ul * tp_next.tv_sec) +
            tp_next.tv_nsec;

        const unsigned long wait_time = 120000000ul / 2;

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


        // -------------- Read Data
        memset(outbuf, 0x00, outbuf_size);

        size_t overruns;
        size_t read = queue.pop(input_buf, input_size, &overruns); // returns bytes

        if (read != input_size) {
            fprintf(stderr, "U");
        }

        if (overruns) {
            fprintf(stderr, "O%zu", overruns);
        }

        // -------------- AAC Encoding

        in_ptr = input_buf;
        in_size = (int)read;
        in_elem_size = BYTES_PER_SAMPLE;
        in_args.numInSamples = input_size/BYTES_PER_SAMPLE;
        in_buf.numBufs = 1;
        in_buf.bufs = &in_ptr;
        in_buf.bufferIdentifiers = &in_identifier;
        in_buf.bufSizes = &in_size;
        in_buf.bufElSizes = &in_elem_size;

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
            if (err == AACENC_ENCODE_EOF)
                break;
            fprintf(stderr, "Encoding failed\n");
            break;
        }
        calls++;

        /* Check if the encoder has generated output data */
        if (out_args.numOutBytes != 0)
        {
            // Our timing code depends on this
            assert (calls == 2);
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
                fprintf(stderr, ".");
        }
    }

    zmq_sock.close();
    free_rs_char(rs_handler);

    aacEncClose(&encoder);

}

