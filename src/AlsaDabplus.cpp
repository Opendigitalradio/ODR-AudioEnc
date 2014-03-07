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

#include "libAACenc/include/aacenc_lib.h"

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
    int err;
    const char *alsa_device = "default";
    const char *outuri = NULL;
    int sample_rate=48000, channels=2;
    const int bytes_per_sample = 2;
    void *rs_handler = NULL;
    int afterburner = 0;
    HANDLE_AACENCODER handle;
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

    if (aacEncInfo(handle, &info) != AACENC_OK) {
        fprintf(stderr, "Unable to get the encoder info\n");
        return 1;
    }

    fprintf(stderr, "DAB+ Encoding: framelen=%d\n", info.frameLength);

    // Each DAB+ frame will need input_size audio bytes
    int input_size = channels * bytes_per_sample * info.frameLength;
    uint8_t input_buf[input_size];

    int max_size = input_size + NUM_SAMPLES_PER_CALL;

    SampleQueue<uint8_t> queue(BYTES_PER_SAMPLE, channels, max_size);

    AlsaInput alsa_in(alsa_device, channels, sample_rate, queue);

    if (alsa_in.prepare() != 0) {
        fprintf(stderr, "Alsa preparation failed\n");
        return 1;
    }


