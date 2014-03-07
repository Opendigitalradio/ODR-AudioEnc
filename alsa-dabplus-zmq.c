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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <alloca.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <zmq.h>
#include <assert.h>
#include "libAACenc/include/aacenc_lib.h"
#include <error.h>
#include <signal.h>
#include <alsa/asoundlib.h>

#include <fec.h>

static struct {
    snd_pcm_format_t format;
    unsigned int channels;
    unsigned int rate;
} hwparams;


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

static snd_pcm_t *alsa_handle = NULL;

static void prg_exit(int code)
{
    if (alsa_handle) {
        snd_pcm_close(alsa_handle);
    }
    exit(code);
}

static void alsa_prepare(const char* alsa_dev, unsigned int rate, unsigned int channels)
{
    int err;
    snd_pcm_hw_params_t *hw_params;

    fprintf(stderr, "Initialising ALSA...\n");

    const int open_mode = 0; //|= SND_PCM_NONBLOCK;

    if ((err = snd_pcm_open(&alsa_handle, alsa_dev, SND_PCM_STREAM_CAPTURE, open_mode)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n",
                alsa_dev, snd_strerror(err));
        prg_exit(1);
    }

    const int nonblock = 0; //TODO remove dead code
    if (nonblock) {
        err = snd_pcm_nonblock(alsa_handle, 1);
        if (err < 0) {
            fprintf(stderr, "nonblock setting error: %s", snd_strerror(err));
            prg_exit(1);
        }
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    if ((err = snd_pcm_hw_params_any(alsa_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    if ((err = snd_pcm_hw_params_set_access(alsa_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "cannot set access type (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    if ((err = snd_pcm_hw_params_set_format(alsa_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
        fprintf (stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    if ((err = snd_pcm_hw_params_set_rate_near(alsa_handle, hw_params, &rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    if ((err = snd_pcm_hw_params_set_channels(alsa_handle, hw_params, channels)) < 0) {
        fprintf (stderr, "cannot set channel count (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    if ((err = snd_pcm_hw_params(alsa_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    snd_pcm_hw_params_free (hw_params);

    if ((err = snd_pcm_prepare(alsa_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        prg_exit(1);
    }

    fprintf(stderr, "ALSA init done.\n");
}

static size_t alsa_read(uint8_t* buf, snd_pcm_uframes_t length)
{
    int i;
    int err;

    err = snd_pcm_readi(alsa_handle, buf, length);

    if (err != length) {
        if (err < 0) {
            fprintf (stderr, "read from audio interface failed (%s)\n",
                    snd_strerror(err));
        }
        else {
            fprintf(stderr, "short alsa read: %d\n", err);
        }
    }

    return err;
}

static void signal_handler(int sig)
{
    fprintf(stderr, "Caught signal %d\n", sig);
    if (alsa_handle) {
        snd_pcm_abort(alsa_handle);
        alsa_handle = NULL;
    }

    if (sig == SIGABRT) {
        /* do not call snd_pcm_close() and abort immediately */
        alsa_handle = NULL;
        exit(EXIT_FAILURE);
    }
    signal(sig, signal_handler);
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
    uint8_t* input_buf;
    int16_t* convert_buf;
    void *rs_handler = NULL;
    int aot = AOT_DABPLUS_AAC_LC;
    int afterburner = 0;
    HANDLE_AACENCODER handle;
    CHANNEL_MODE mode;
    AACENC_InfoStruct info = { 0 };

    void *zmq_context = zmq_ctx_new();
    void *zmq_sock = NULL;

    const struct option longopts[] = {
        {"bitrate",     required_argument,  0, 'b'},
        {"output",      required_argument,  0, 'o'},
        {"device",      required_argument,  0, 'd'},
        {"rate",        required_argument,  0, 'r'},
        {"channels",    required_argument,  0, 'c'},
        //{"lp",          no_argument,        0, 'l'},
        {"afterburner", no_argument,        0, 'a'},
        {"help",        no_argument,        0, 'h'},
        {0,0,0,0},
    };

    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "lhab:c:o:r:d:", longopts, &index);
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
        fprintf(stderr, "Bad subchannels number: %d, try other bitrate.\n", subchannel_index);
        return 1;
    }

    fprintf(stderr, "Setting up ZeroMQ socket\n");
    if (outuri) {
        zmq_sock = zmq_socket(zmq_context, ZMQ_PUB);
        if (zmq_sock == NULL) {
            fprintf(stderr, "Error occurred during zmq_socket: %s\n", zmq_strerror(errno));
            return 2;
        }
        if (zmq_connect(zmq_sock, outuri) != 0) {
            fprintf(stderr, "Error occurred during zmq_connect: %s\n", zmq_strerror(errno));
            return 2;
        }
    } else {
        fprintf(stderr, "Output URI not defined\n");
        return 1;
    }

    alsa_prepare(alsa_device, sample_rate, channels);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGABRT, signal_handler);

    switch (channels) {
    case 1: mode = MODE_1;       break;
    case 2: mode = MODE_2;       break;
    default:
        fprintf(stderr, "Unsupported channels number %d\n", channels);
        prg_exit(1);
    }


    if (aacEncOpen(&handle, 0x01|0x02|0x04, channels) != AACENC_OK) {
        fprintf(stderr, "Unable to open encoder\n");
        prg_exit(1);
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
        prg_exit(1);
    }
    if (aacEncoder_SetParam(handle, AACENC_SAMPLERATE, sample_rate) != AACENC_OK) {
        fprintf(stderr, "Unable to set the AOT\n");
        prg_exit(1);
    }
    if (aacEncoder_SetParam(handle, AACENC_CHANNELMODE, mode) != AACENC_OK) {
        fprintf(stderr, "Unable to set the channel mode\n");
        prg_exit(1);
    }
    if (aacEncoder_SetParam(handle, AACENC_CHANNELORDER, 1) != AACENC_OK) {
        fprintf(stderr, "Unable to set the wav channel order\n");
        prg_exit(1);
    }
    if (aacEncoder_SetParam(handle, AACENC_GRANULE_LENGTH, 960) != AACENC_OK) {
        fprintf(stderr, "Unable to set the AOT\n");
        prg_exit(1);
    }
    if (aacEncoder_SetParam(handle, AACENC_TRANSMUX, TT_DABPLUS) != AACENC_OK) {
        fprintf(stderr, "Unable to set the RAW transmux\n");
        prg_exit(1);
    }

    /*if (aacEncoder_SetParam(handle, AACENC_BITRATEMODE, 7 *AACENC_BR_MODE_SFR*) != AACENC_OK) {
        fprintf(stderr, "Unable to set the bitrate mode\n");
        prg_exit(1);
    }*/


    fprintf(stderr, "AAC bitrate set to: %d\n", subchannel_index*8000);
    if (aacEncoder_SetParam(handle, AACENC_BITRATE, subchannel_index*8000) != AACENC_OK) {
        fprintf(stderr, "Unable to set the bitrate\n");
        prg_exit(1);
    }
    if (aacEncoder_SetParam(handle, AACENC_AFTERBURNER, afterburner) != AACENC_OK) {
        fprintf(stderr, "Unable to set the afterburner mode\n");
        prg_exit(1);
    }
    if (aacEncEncode(handle, NULL, NULL, NULL, NULL) != AACENC_OK) {
        fprintf(stderr, "Unable to initialize the encoder\n");
        prg_exit(1);
    }
    if (aacEncInfo(handle, &info) != AACENC_OK) {
        fprintf(stderr, "Unable to get the encoder info\n");
        prg_exit(1);
    }

    fprintf(stderr, "DAB+ Encoding: framelen=%d\n", info.frameLength);

    int input_size = channels * bytes_per_sample * info.frameLength;
    input_buf = (uint8_t*) malloc(input_size);
    convert_buf = (int16_t*) malloc(input_size);

    /* symsize=8, gfpoly=0x11d, fcr=0, prim=1, nroots=10, pad=135 */
    rs_handler = init_rs_char(8, 0x11d, 0, 1, 10, 135);
    if (rs_handler == NULL) {
        perror("init_rs_char failed");
        prg_exit(1);
    }

    int loops = 0;
    int outbuf_size = subchannel_index*120;
    uint8_t outbuf[20480];

    if(outbuf_size % 5 != 0) {
        fprintf(stderr, "(outbuf_size mod 5) = %d\n", outbuf_size % 5);
    }

    fprintf(stderr, "outbuf_size: %d\n", outbuf_size);
    //outbuf_size += (4 * subchannel_index * (8*8)/8) - outbuf_size/5;
    //fprintf(stderr, "outbuf_size: %d\n", outbuf_size);

    int frame=0;
    int send_error_count = 0;
    while (1) {
        memset(outbuf, 0x00, outbuf_size);

        AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
        AACENC_InArgs in_args = { 0 };
        AACENC_OutArgs out_args = { 0 };
        int in_identifier = IN_AUDIO_DATA;
        int in_size, in_elem_size;
        int out_identifier = OUT_BITSTREAM_DATA;
        int out_size, out_elem_size;
        int i;
        int send_error;
        void *in_ptr, *out_ptr;
        AACENC_ERROR err;

        int readframes = alsa_read(input_buf, info.frameLength);
        if (readframes != info.frameLength) {
            fprintf(stderr, "Unable to read enough data from input!\n");
            break;
        }

        readframes*=2;
#if 1
        for (i = 0; i < readframes; i++) {
            const uint8_t* in = &input_buf[2*i];
            convert_buf[i] = in[0] | (in[1] << 8);
        }
#endif

        if (readframes <= 0) {
            in_args.numInSamples = -1;
        } else {
            in_ptr = input_buf;
            in_size = readframes*2;
            in_elem_size = 2;

            in_args.numInSamples = readframes;
            in_buf.numBufs = 1;
            in_buf.bufs = &in_ptr;
            in_buf.bufferIdentifiers = &in_identifier;
            in_buf.bufSizes = &in_size;
            in_buf.bufElSizes = &in_elem_size;
        }
        out_ptr = outbuf;
        out_size = sizeof(outbuf);
        out_elem_size = 1;
        out_buf.numBufs = 1;
        out_buf.bufs = &out_ptr;
        out_buf.bufferIdentifiers = &out_identifier;
        out_buf.bufSizes = &out_size;
        out_buf.bufElSizes = &out_elem_size;

        if ((err = aacEncEncode(handle, &in_buf, &out_buf, &in_args, &out_args)) != AACENC_OK) {
            if (err == AACENC_ENCODE_EOF)
                break;
            fprintf(stderr, "Encoding failed\n");
            prg_exit(1);
        }
        if (out_args.numOutBytes == 0)
            continue;
#if 0
        unsigned char au_start[6];
        unsigned char* sfbuf = outbuf;
        au_start[0] = 6;
        au_start[1] = (*(sfbuf + 3) << 4) + ((*(sfbuf + 4)) >> 4);
        au_start[2] = ((*(sfbuf + 4) & 0x0f) << 8) + *(sfbuf + 5);
        fprintf (stderr, "au_start[0] = %d\n", au_start[0]);
        fprintf (stderr, "au_start[1] = %d\n", au_start[1]);
        fprintf (stderr, "au_start[2] = %d\n", au_start[2]);
#endif

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

        send_error = zmq_send(zmq_sock, outbuf, outbuf_size, ZMQ_DONTWAIT);
        if (send_error < 0) {
            fprintf(stderr, "ZeroMQ send failed! %s\n", zmq_strerror(errno));
            send_error_count ++;
        }

        if (send_error_count > 10)
        {
            fprintf(stderr, "ZeroMQ send failed ten times, aborting!\n");
            break;
        }
        //fwrite(outbuf, 1, /*out_args.numOutBytes*/ outbuf_size, out_fh);
        //fprintf(stderr, "Written %d/%d bytes!\n", out_args.numOutBytes + row*10, outbuf_size);
        if(out_args.numOutBytes + row*10 == outbuf_size)
            fprintf(stderr, ".");

//      if(frame > 10)
//          break;
        frame++;
    }

    zmq_close(zmq_sock);
    free_rs_char(rs_handler);

    aacEncClose(&handle);

    zmq_ctx_term(zmq_context);
    prg_exit(0);
}

