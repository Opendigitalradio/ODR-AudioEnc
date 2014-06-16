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
#include "FileInput.h"
#include "SampleQueue.h"
#include "zmq.hpp"

extern "C" {
#include "encryption.h"
#include "utils.h"
#include "wavreader.h"
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
    "dabplus-enc %s is a HE-AACv2 encoder for DAB+\n"
    "based on fdk-aac-dabplus that can read from a ALSA or file source\n"
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
    "When this option is enabled, you will see U and O printed in the\n"
    "console. These correspond to audio underruns and overruns caused\n"
    "by sound card clock drift. When sparse, they should not create audible\n"
    "artifacts.\n"
    "\n"
    "This encoder includes PAD (DLS and MOT Slideshow) support by\n"
    "http://rd.csp.it to be used with mot-encoder\n"
    "\n"
    "  http://opendigitalradio.org\n"
    "\nUsage:\n"
    "%s (-i file|-d alsa_device) [OPTION...]\n",
#if defined(GITVERSION)
    GITVERSION
#else
    PACKAGE_VERSION
#endif
    , name);
    fprintf(stderr,
    "   For the alsa input:\n"
    "     -d, --device=alsa_device             Set ALSA input device (default: \"default\").\n"
    "     -D, --drift-comp                     Enable ALSA sound card drift compensation.\n"
    "   For the file input:\n"
    "     -i, --input=FILENAME                 Input filename (default: stdin).\n"
    "     -f, --format={ wav, raw }            Set input file format (default: wav).\n"
    "         --fifo-silence                   Input file is fifo and encoder generates silence when fifo is empty. Ignore EOF.\n"
    "   Encoder parameters:\n"
    "     -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.\n"
    "     -a, --afterburner                    Turn on AAC encoder quality increaser.\n"
    "     -c, --channels={ 1, 2 }              Nb of input channels (default: 2).\n"
    "     -r, --rate={ 32000, 48000 }          Input sample rate (default: 48000).\n"
    "         --aaclc                          Force the usage of AAC-LC (no SBR, no PS)\n"
    "         --sbr                            Force the usage of SBR\n"
    "         --ps                             Force the usage of PS\n"
    "   Output and pad parameters:\n"
    "     -o, --output=URI                     Output zmq uri. (e.g. 'tcp://*:9000')\n"
    "                                     -or- Output file uri. (e.g. 'file.dab')\n"
    "                                     -or- a single dash '-' to denote stdout\n"
    "     -k, --secret-key=FILE                Enable ZMQ encryption with the given secret key.\n"
    "     -p, --pad=BYTES                      Set PAD size in bytes.\n"
    "     -P, --pad-fifo=FILENAME              Set PAD data input fifo name"
    "                                          (default: /tmp/pad.fifo).\n"
    "     -l, --level                          Show peak audio level indication.\n"
    "\n"
    "Only the tcp:// zeromq transport has been tested until now,\n"
    " but epgm:// and pgm:// are also accepted\n"
    );

}

int prepare_aac_encoder(
        HANDLE_AACENCODER *encoder,
        int subchannel_index,
        int channels,
        int sample_rate,
        int afterburner,
        int *aot)
{
    HANDLE_AACENCODER handle = *encoder;

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

    if (*aot == AOT_NONE) {

        if(channels == 2 && subchannel_index <= 6) {
            *aot = AOT_DABPLUS_PS;
        }
        else if((channels == 1 && subchannel_index <= 8) || subchannel_index <= 10) {
            *aot = AOT_DABPLUS_SBR;
        }
        else {
            *aot = AOT_DABPLUS_AAC_LC;
        }
    }

    fprintf(stderr, "Using %d subchannels. AAC type: %s%s%s. channels=%d, sample_rate=%d\n",
            subchannel_index,
            *aot == AOT_DABPLUS_PS ? "HE-AAC v2" : "",
            *aot == AOT_DABPLUS_SBR ? "HE-AAC" : "",
            *aot == AOT_DABPLUS_AAC_LC ? "AAC-LC" : "",
            channels, sample_rate);

    if (aacEncoder_SetParam(handle, AACENC_AOT, *aot) != AACENC_OK) {
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

#define STATUS_PAD_INSERTED 0x1
#define STATUS_OVERRUN 0x2
#define STATUS_UNDERRUN 0x4

int main(int argc, char *argv[])
{
    int subchannel_index = 8; //64kbps subchannel
    int ch=0;

    // For the ALSA input
    const char *alsa_device = NULL;

    // For the file input
    const char *infile = NULL;
    int raw_input = 0;

    // For the file output
    FILE *out_fh = NULL;

    const char *outuri = NULL;
    int sample_rate=48000, channels=2;
    const int bytes_per_sample = 2;
    void *rs_handler = NULL;
    bool afterburner = false;
    bool inFifoSilence = false;
    bool drift_compensation = false;
    AACENC_InfoStruct info = { 0 };
    int aot = AOT_NONE;

    /* Keep track of peaks */
    int peak_left  = 0;
    int peak_right = 0;

    /* For MOT Slideshow and DLS insertion */
    const char* pad_fifo = "/tmp/pad.fifo";
    int pad_fd;
    unsigned char pad_buf[128];
    int padlen = 0;

    /* Encoder status, see the above STATUS macros */
    int status = 0;

    /* Whether to show the 'sox'-like measurement */
    int show_level = 0;

    /* Data for ZMQ CURVE authentication */
    char* keyfile = NULL;
    char secretkey[CURVE_KEYLEN+1];

    const struct option longopts[] = {
        {"bitrate",       required_argument,  0, 'b'},
        {"channels",      required_argument,  0, 'c'},
        {"device",        required_argument,  0, 'd'},
        {"format",        required_argument,  0, 'f'},
        {"input",         required_argument,  0, 'i'},
        {"output",        required_argument,  0, 'o'},
        {"pad",           required_argument,  0, 'p'},
        {"pad-fifo",      required_argument,  0, 'P'},
        {"rate",          required_argument,  0, 'r'},
        {"secret-key",    required_argument,  0, 'k'},
        {"afterburner",   no_argument,        0, 'a'},
        {"drift-comp",    no_argument,        0, 'D'},
        {"help",          no_argument,        0, 'h'},
        {"level",         no_argument,        0, 'l'},
        {"aaclc",         no_argument,        0, 0  },
        {"sbr",           no_argument,        0, 1  },
        {"ps",            no_argument,        0, 2  },
        {"fifo-silence",   no_argument,        0, 3  },
        {0,0,0,0},
    };

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "ahDlb:c:f:i:k:o:r:d:p:P:", longopts, &index);
        switch (ch) {
        case 0: // AAC-LC
            aot = AOT_DABPLUS_AAC_LC;
            break;
        case 1: // SBR
            aot = AOT_DABPLUS_SBR;
            break;
        case 2: // PS
            aot = AOT_DABPLUS_PS;
            break;
        case 3: // FIFO SILENCE
            inFifoSilence = true;
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
        case 'd':
            alsa_device = optarg;
            break;
        case 'D':
            drift_compensation = true;
            break;
        case 'f':
            if(strcmp(optarg, "raw")==0) {
                raw_input = 1;
            } else if(strcmp(optarg, "wav")!=0)
                usage(argv[0]);
            break;
        case 'i':
            infile = optarg;
            break;
        case 'k':
            keyfile = optarg;
            break;
        case 'l':
            show_level = 1;
            break;
        case 'o':
            outuri = optarg;
            break;
        case 'p':
            padlen = atoi(optarg);
            break;
        case 'P':
            pad_fifo = optarg;
            break;
        case 'r':
            sample_rate = atoi(optarg);
            break;
        case '?':
        case 'h':
            usage(argv[0]);
            return 1;
        }
    }

    if (alsa_device && infile) {
        fprintf(stderr, "You must define either alsa or file input, not both\n");
        return 1;
    }

    if (subchannel_index < 1 || subchannel_index > 24) {
        fprintf(stderr, "Bad subchannels number: %d, try other bitrate.\n",
                subchannel_index);
        return 1;
    }

    if ( ! (sample_rate == 32000 || sample_rate == 48000)) {
        fprintf(stderr, "Invalid sample rate. Possible values are: 32000, 48000.\n");
        return 1;
    }

    zmq::context_t zmq_ctx;
    zmq::socket_t zmq_sock(zmq_ctx, ZMQ_PUB);

    if (outuri) {
        if (strcmp(outuri, "-") == 0) {
            out_fh = stdout;
        }
        else if ((strncmp(outuri, "tcp://", 6) == 0) ||
                (strncmp(outuri, "pgm://", 6) == 0) ||
                (strncmp(outuri, "epgm://", 7) == 0)) {
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
        }
        else { // We assume it's a file name
            out_fh = fopen(outuri, "wb");

            if (!out_fh) {
                fprintf(stderr, "Can't open output file!\n");
                return 1;
            }
        }
    }
    else {
        fprintf(stderr, "Output URI not defined\n");
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


    HANDLE_AACENCODER encoder;

    if (prepare_aac_encoder(&encoder, subchannel_index, channels,
                sample_rate, afterburner, &aot) != 0) {
        fprintf(stderr, "Encoder preparation failed\n");
        return 2;
    }

    /* We assume that we need to call the encoder
     * enc_calls_per_output before it gives us one encoded audio
     * frame. This information is used when the alsa drift compensation
     * is active
     */
    const int enc_calls_per_output =
        (aot == AOT_DABPLUS_AAC_LC) ? sample_rate / 8000 : sample_rate / 16000;


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

    /* No input defined ? default to alsa "default" */
    if (!alsa_device) {
        alsa_device = "default";
    }

    // We'll use one of the tree possible inputs
    AlsaInputThreaded alsa_in_threaded(alsa_device, channels, sample_rate, queue);
    AlsaInputDirect   alsa_in_direct(alsa_device, channels, sample_rate);
    FileInput         file_in(infile, raw_input, sample_rate);

    if (infile) {
        if (file_in.prepare() != 0) {
            fprintf(stderr, "File input preparation failed\n");
            return 1;
        }
    }
    else if (drift_compensation) {
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
    uint8_t zmqframebuf[2048];
    zmq_frame_header_t *zmq_frame_header = (zmq_frame_header_t*)zmqframebuf;

    uint8_t outbuf[2048];

    if(outbuf_size % 5 != 0) {
        fprintf(stderr, "(outbuf_size mod 5) = %d\n", outbuf_size % 5);
    }

    fprintf(stderr, "Starting encoding\n");

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
                status |= STATUS_PAD_INSERTED;
        }
        else {
            // Some other error occurred during read.
            fprintf(stderr, "Unable to read from PAD!\n");
                        break;
        }

        // -------------- Read Data
        memset(outbuf, 0x00, outbuf_size);
        memset(input_buf, 0x00, input_size);

        ssize_t read;
        if (infile) {
            read = file_in.read(input_buf, input_size);
            if (read < 0) {
                break;
            }
            else if (read != input_size) {
                if (inFifoSilence && file_in.eof()) {
                   memset(input_buf, 0, input_size);
                   read = input_size;
                   usleep((long int)input_size*1000000/(bytes_per_sample*channels*sample_rate));
                } else {
                   fprintf(stderr, "Short file read !\n");
                   break;
               }
            }
        }
        else if (drift_compensation) {
            if (alsa_in_threaded.fault_detected()) {
                fprintf(stderr, "Detected fault in alsa input!\n");
                break;
            }

            size_t overruns;
            read = queue.pop(input_buf, input_size, &overruns); // returns bytes

            if (read != input_size) {
                status |= STATUS_UNDERRUN;
            }

            if (overruns) {
                status |= STATUS_OVERRUN;
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

        for (int i = 0; i < read; i+=4) {
            int16_t l = input_buf[i] | (input_buf[i+1] << 8);
            int16_t r = input_buf[i+2] | (input_buf[i+3] << 8);
            peak_left  = MAX(peak_left,  l);
            peak_right = MAX(peak_right, r);
        }

        // -------------- AAC Encoding

        int calculated_padlen = ret > 0 ? padlen : 0;


        in_ptr[0] = input_buf;
        in_ptr[1] = pad_buf;
        in_size[0] = read;
        in_size[1] = calculated_padlen;
        in_elem_size[0] = BYTES_PER_SAMPLE;
        in_elem_size[1] = sizeof(uint8_t);
        in_args.numInSamples = input_size/BYTES_PER_SAMPLE;
        in_args.numAncBytes = calculated_padlen;

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
            if (calls != enc_calls_per_output) {
                fprintf(stderr, "INTERNAL ERROR! calls=%d"
                        ", expected %d\n",
                        calls, enc_calls_per_output);
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

            if (out_fh) {
                fwrite(outbuf, 1, outbuf_size, out_fh);
            }
            else {
                // ------------ ZeroMQ transmit
                try {
                    zmq_frame_header->version = 1;
                    zmq_frame_header->encoder = ZMQ_ENCODER_FDK;
                    zmq_frame_header->datasize = outbuf_size;
                    zmq_frame_header->audiolevel_left = peak_left;
                    zmq_frame_header->audiolevel_right = peak_right;

                    memcpy(ZMQ_FRAME_DATA(zmq_frame_header),
                            outbuf, outbuf_size);

                    zmq_sock.send(zmqframebuf, ZMQ_FRAME_SIZE(zmq_frame_header),
                            ZMQ_DONTWAIT);
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
            }

            if (out_args.numOutBytes + row*10 == outbuf_size) {
                if (show_level) {
                    fprintf(stderr, "\rIn: [%6s|%-6s] %1s %1s %1s",
                            level(0, &peak_left),
                            level(1, &peak_right),
                            status & STATUS_PAD_INSERTED ? "P" : " ",
                            status & STATUS_UNDERRUN ? "U" : " ",
                            status & STATUS_OVERRUN ? "O" : " ");
                }
                else {
                    if (status & STATUS_OVERRUN) {
                        fprintf(stderr, "O");
                    }

                    if (status & STATUS_UNDERRUN) {
                        fprintf(stderr, "U");
                    }
                }
            }

            status = 0;
        }

        fflush(stdout);
    }
    fprintf(stderr, "\n");

    if (out_fh) {
        fclose(out_fh);
    }

    zmq_sock.close();
    free_rs_char(rs_handler);

    aacEncClose(&encoder);
}

