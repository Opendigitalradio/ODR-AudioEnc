/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2013,2014 Matthias P. Braendli
 * Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l.
 *      http://rd.csp.it/
 *
 * http://opendigitalradio.org
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program  If not, see <http://www.gnu.org/licenses/>.
 * -------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <zmq.h>
#include <assert.h>
#include "libAACenc/include/aacenc_lib.h"
#include "wavreader.h"
#include "encryption.h"

#include <fec.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "contrib/lib_crc.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

void usage(const char* name) {
    fprintf(stderr,
    "dabplus-enc-file-zmq %s is a HE-AACv2 encoder for DAB+\n"
    "based on fdk-aac-dabplus that can read from a file\n"
    "or pipe source and encode to a ZeroMQ output for ODR-DabMux,\n"
    "a file or standard output.\n"
    "\n"
    "It includes PAD (DLS and MOT Slideshow) support by http://rd.csp.it\n"
    "to be used with mot-encoder\n"
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
    "     -i, --input=FILENAME                 Input filename (default: stdin).\n"
    "     -o, --output=URI                     Output zmq uri or filename.\n"
    "                                          Use '-' for standard output,\n"
    "                                          'tcp://odr-dabmux-host:9000' for ZeroMQ.\n"
    "                                          Protocols supported: tcp, pgm, epgm\n"
    "     -a, --afterburner                    Turn on AAC encoder quality increaser.\n"
    "     -p, --pad=BYTES                      Set PAD size in bytes.\n"
    "     -P, --pad-fifo=FILENAME              Set PAD data input fifo name (default: /tmp/pad.fifo).\n"
    "     -f, --format={ wav, raw }            Set input file format (default: wav).\n"
    "     -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).\n"
    "     -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).\n"
    "     -k, --secret-key=FILE                Set the secret key for encryption.\n"
    "     -s, --suppress-dots                  Do not show the little dots.\n"
    //"   -v, --verbose=LEVEL                  Set verbosity level.\n"
    "\n"
    "Only the tcp:// zeromq transport has been tested until now.\n"

    );

}


#define no_argument 0
#define required_argument 1
#define optional_argument 2

int main(int argc, char *argv[]) {
    int subchannel_index = 8; //64kbps subchannel
    int ch=0;
    const char *infile = NULL;
    const char *outuri = NULL;
    FILE *in_fh;
    FILE *out_fh;
    void *wav;
    int wav_format, bits_per_sample, sample_rate=48000, channels=2;
    uint8_t* input_buf;
    int16_t* convert_buf;
    void *rs_handler = NULL;
    int aot = AOT_DABPLUS_AAC_LC;
    int afterburner = 0, raw_input=0;
    HANDLE_AACENCODER handle;
    CHANNEL_MODE mode;
    AACENC_InfoStruct info = { 0 };

    char* pad_fifo = "/tmp/pad.fifo";
    int pad_fd;
    unsigned char pad_buf[128];
    int padlen;

    void *zmq_context = zmq_ctx_new();
    void *zmq_sock = NULL;

    int show_dots = 1;

    /* Data for ZMQ CURVE authentication */
    char* keyfile = NULL;
    char secretkey[CURVE_KEYLEN+1];

    const struct option longopts[] = {
        {"bitrate",       required_argument,  0, 'b'},
        {"input",         required_argument,  0, 'i'},
        {"output",        required_argument,  0, 'o'},
        {"format",        required_argument,  0, 'f'},
        {"rate",          required_argument,  0, 'r'},
        {"channels",      required_argument,  0, 'c'},
        {"pad",           required_argument,  0, 'p'},
        {"pad-fifo",      required_argument,  0, 'P'},
        {"secret-key",    required_argument,  0, 'k'},
        {"afterburner",   no_argument,        0, 'a'},
        {"help",          no_argument,        0, 'h'},
        {"suppress-dots", no_argument,        0, 's'},
        {0,0,0,0},
    };

    if (argc == 1) {
        usage(argv[0]);
        return 0;
    }

    int index;
    while(ch != -1) {
        ch = getopt_long(argc, argv, "tlhab:c:i:k:o:r:f:p:P:s", longopts, &index);
        switch (ch) {
        case 'f':
            if(strcmp(optarg, "raw")==0) {
                raw_input = 1;
            } else if(strcmp(optarg, "wav")!=0)
                usage(argv[0]);
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
        case 'i':
            infile = optarg;
            break;
        case 'k':
            keyfile = optarg;
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
        case 's':
            show_dots = 0;
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
    if(padlen != 0) {
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

    if(raw_input) {
        if(infile && strcmp(infile, "-")) {
            in_fh = fopen(infile, "rb");
            if(!in_fh) {
                fprintf(stderr, "Can't open input file!\n");
                return 1;
            }
        } else {
            in_fh = stdin;
        }
    } else {
        wav = wav_read_open(infile);
        if (!wav) {
            fprintf(stderr, "Unable to open wav file %s\n", infile);
            return 1;
        }
        if (!wav_get_header(wav, &wav_format, &channels, &sample_rate, &bits_per_sample, NULL)) {
            fprintf(stderr, "Bad wav file %s\n", infile);
            return 1;
        }
        if (wav_format != 1) {
            fprintf(stderr, "Unsupported WAV format %d\n", wav_format);
            return 1;
        }
        if (bits_per_sample != 16) {
            fprintf(stderr, "Unsupported WAV sample depth %d\n", bits_per_sample);
            return 1;
        }
        if (channels > 2) {
            fprintf(stderr, "Unsupported WAV channels %d\n", channels);
            return 1;
        }
    }

    if (outuri) {
        if (strcmp(outuri, "-") == 0) {
            out_fh = stdout;
        }
        else if (strncmp(outuri, "file://", 7) == 0) {
            out_fh = fopen(&outuri[7], "wb");

            if(!out_fh) {
                fprintf(stderr, "Can't open output file!\n");
                return 1;
            }
        }
        else if ((strncmp(outuri, "tcp://", 6) == 0) ||
                (strncmp(outuri, "pgm://", 6) == 0) ||
                (strncmp(outuri, "epgm://", 7) == 0)) {
            zmq_sock = zmq_socket(zmq_context, ZMQ_PUB);
            if (zmq_sock == NULL) {
                fprintf(stderr, "Error occurred during zmq_socket: %s\n",
                        zmq_strerror(errno));
                return 2;
            }
            if (keyfile) {
                fprintf(stderr, "Enabling encryption\n");

                int rc = readkey(keyfile, secretkey);
                if (rc) {
                    fprintf(stderr, "Error reading secret key\n");
                    return 2;
                }

                const int yes = 1;
                rc = zmq_setsockopt(zmq_sock, ZMQ_CURVE_SERVER,
                        &yes, sizeof(yes));
                if (rc) {
                    fprintf(stderr, "Error: %s\n", zmq_strerror(errno));
                    return 2;
                }

                rc = zmq_setsockopt(zmq_sock, ZMQ_CURVE_SECRETKEY,
                        secretkey, CURVE_KEYLEN);
                if (rc) {
                    fprintf(stderr, "Error: %s\n", zmq_strerror(errno));
                    return 2;
                }
            }
            if (zmq_connect(zmq_sock, outuri) != 0) {
                fprintf(stderr, "Error occurred during zmq_connect: %s\n",
                        zmq_strerror(errno));
                return 2;
            }
        }
        else {
            out_fh = fopen(outuri, "wb");

            if(!out_fh) {
                fprintf(stderr, "Can't open output file!\n");
                return 1;
            }
        }
    } else {
        fprintf(stderr, "Output URI not defined\n");
        return 1;
    }


    switch (channels) {
    case 1: mode = MODE_1;       break;
    case 2: mode = MODE_2;       break;
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
        fprintf(stderr, "Unable to set the samplerate\n");
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

    /*if (aacEncoder_SetParam(handle, AACENC_BITRATEMODE, 7 *AACENC_BR_MODE_SFR*) != AACENC_OK) {
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
    if (aacEncoder_SetParam(handle, AACENC_ANCILLARY_BITRATE, 0) != AACENC_OK) {
        fprintf(stderr, "Unable to set the ancillary bitrate\n");
        return 1;
    }
    if (aacEncEncode(handle, NULL, NULL, NULL, NULL) != AACENC_OK) {
        fprintf(stderr, "Unable to initialize the encoder\n");
        return 1;
    }
    if (aacEncInfo(handle, &info) != AACENC_OK) {
        fprintf(stderr, "Unable to get the encoder info\n");
        return 1;
    }

    fprintf(stderr, "DAB+ Encoding: framelen=%d\n", info.frameLength);

    int input_size = channels*2*info.frameLength;
    input_buf = (uint8_t*) malloc(input_size);
    convert_buf = (int16_t*) malloc(input_size);

    /* symsize=8, gfpoly=0x11d, fcr=0, prim=1, nroots=10, pad=135 */
    rs_handler = init_rs_char(8, 0x11d, 0, 1, 10, 135);
    if (rs_handler == NULL) {
        perror("init_rs_char failed");
        return 0;
    }

    int loops = 0;
    int outbuf_size = subchannel_index*120;
    uint8_t outbuf[20480];

    if(outbuf_size % 5 != 0) {
        fprintf(stderr, "(outbuf_size mod 5) = %d\n", outbuf_size % 5);
    }

    fprintf(stderr, "outbuf_size: %d\n", outbuf_size);
    //outbuf_size += (4 * subchannel_index * (8*8)/8) - outbuf_size/5;
    fprintf(stderr, "outbuf_size: %d\n", outbuf_size);

    int frame=0;
    int send_error_count = 0;
    while (1) {
        memset(outbuf, 0x00, outbuf_size);

        AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
        AACENC_InArgs in_args = { 0 };
        AACENC_OutArgs out_args = { 0 };
        int in_identifier[] = {IN_AUDIO_DATA, IN_ANCILLRY_DATA};
        int in_size[2], in_elem_size[2];
        int out_identifier = OUT_BITSTREAM_DATA;
        int out_size, out_elem_size;
        int pcmread=0, i, ret;
        int send_error;
        void *in_ptr[2], *out_ptr;
        AACENC_ERROR err;

        // Read data from the PAD fifo
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
                fprintf(stderr, "p");
        }
        else {
            // Some other error occurred during read.
            fprintf(stderr, "Unable to read from PAD!\n");
                        break;
        }

        if(raw_input) {
            if(fread(input_buf, input_size, 1, in_fh) == 1) {
                pcmread = input_size;
            } else {
                fprintf(stderr, "Unable to read from input!\n");
                break;
            }
        } else {
            pcmread = wav_read_data(wav, input_buf, input_size);
        }

        for (i = 0; i < pcmread/2; i++) {
            const uint8_t* in = &input_buf[2*i];
            convert_buf[i] = in[0] | (in[1] << 8);
        }

        if (pcmread <= 0) {
            in_args.numInSamples = -1;
        } else {
            in_ptr[0] = convert_buf;
            in_ptr[1] = pad_buf;
            in_size[0] = pcmread;
            in_size[1] = padlen;

            in_elem_size[0] = 2;
            in_elem_size[1] = sizeof(UCHAR);

            in_args.numInSamples = pcmread/2;
            in_args.numAncBytes = padlen;

            //in_buf.numBufs = 2;    // Samples + Data

            in_buf.bufs = (void**)&in_ptr;
            in_buf.bufferIdentifiers = in_identifier;
            in_buf.bufSizes = in_size;
            in_buf.bufElSizes = in_elem_size;

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
            return 1;
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

        if (zmq_sock) {
            send_error = zmq_send(zmq_sock, outbuf, outbuf_size, ZMQ_DONTWAIT);
            if (send_error < 0) {
                fprintf(stderr, "ZeroMQ send failed! %s\n", zmq_strerror(errno));
                send_error_count ++;
            }
        }
        else {
            fwrite(outbuf, 1, /*out_args.numOutBytes*/ outbuf_size, out_fh);
        }

        if (send_error_count > 10)
        {
            fprintf(stderr, "ZeroMQ send failed ten times, aborting!\n");
            break;
        }
        //fwrite(outbuf, 1, /*out_args.numOutBytes*/ outbuf_size, out_fh);
        //fprintf(stderr, "Written %d/%d bytes!\n", out_args.numOutBytes + row*10, outbuf_size);
        if (show_dots &&
                out_args.numOutBytes + row*10 == outbuf_size)
            fprintf(stderr, ".");

//      if(frame > 10)
//          break;
        frame++;
    }
    free(input_buf);
    free(convert_buf);
    if(raw_input) {
        fclose(in_fh);
    } else {
        wav_read_close(wav);
    }

    if (zmq_sock) {
        zmq_close(zmq_sock);
        zmq_ctx_term(zmq_context);
    }
    else {
        fclose(out_fh);
    }

    free_rs_char(rs_handler);

    aacEncClose(&handle);

    return 0;
}

