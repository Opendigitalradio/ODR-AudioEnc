/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
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
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include "libAACenc/include/aacenc_lib.h"
#include "wavreader.h"

#include <fec.h>

void usage(const char* name) {
	fprintf(stderr, "%s [OPTION...]\n", name);
	fprintf(stderr,
"	  -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.\n"
//"	  -d, --data=FILENAME                  Set data filename.\n"
//"	  -g, --fs-bug                         Turn on FS bug mitigation.\n"
"	  -i, --input=FILENAME                 Input filename (default: stdin).\n"
"	  -o, --output=FILENAME                Output filename (default: stdout).\n"
"	  -a, --afterburner                    Turn on AAC encoder quality increaser.\n"
//"	  -m, --message                        Turn on AAC frame messages.\n"
//"	  -p, --pad=BYTES                      Set PAD size in bytes.\n"
"	  -f, --format={ wav, raw }            Set input file format (default: wav).\n"
"	  -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).\n"
"	  -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).\n"
//"	  -t, --type=TYPE                      Set data type (dls|pad|packet|dg).\n"
//"	  -v, --verbose=LEVEL                  Set verbosity level.\n"
//"	  -V, --version                        Print version and exit.\n"
//"	  --mi=[ 0, ... ]                      Set AAC frame messages interval in milliseconds.\n"
//"	  --ma=[ 0, ... ]                      Set AAC frame messages attack time in milliseconds.\n"
//"	  -t, --adts                           Set ADTS output format (for debugging).\n"
//"	  -l, --lp                             Set frame size to 1024 instead of 960.\n"

);

}


#define no_argument 0
#define required_argument 1
#define optional_argument 2

#define ADTS_HEADER_SIZE 7
#define ADTS_MPEG_ID 1 /* 0: MPEG-4, 1: MPEG-2 */
#define ADTS_MPEG_PROFILE 1
const int mpeg4audio_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

int FindSRIndex(int sr)
{
    int i;
    for (i = 0; i < 16; i++) {
	if (sr == mpeg4audio_sample_rates[i])
	    return i;
    }
    return 16 - 1;
}

void adts_hdr_up(char *buff, int size)
{
    unsigned short len = size + ADTS_HEADER_SIZE;
    unsigned short buffer_fullness = 0x07FF;

    /* frame length, 13 bits */
    buff[3] &= 0xFC;
    buff[3] |= ((len >> 11) & 0x03);	/* 2b: aac_frame_length */
    buff[4] = len >> 3;			/* 8b: aac_frame_length */
    buff[5] = (len << 5) & 0xE0;	/* 3b: aac_frame_length */
    /* buffer fullness, 11 bits */
    buff[5] |= ((buffer_fullness >> 6) & 0x1F);	/* 5b: adts_buffer_fullness */
    buff[6] = (buffer_fullness << 2) & 0xFC;	/* 6b: adts_buffer_fullness */
						/* 2b: num_raw_data_blocks */
}

int main(int argc, char *argv[]) {
	int subchannel_index = 8; //64kbps subchannel
	int ch=0;
	const char *infile, *outfile;
	FILE *in_fh, *out_fh;
	void *wav;
	int wav_format, bits_per_sample, sample_rate=48000, channels=2;
	uint8_t* input_buf;
	int16_t* convert_buf;
	uint8_t* rs_encoded_buf;
	void *rs_handler = NULL;
	int aot = AOT_DABPLUS_AAC_LC;
	int afterburner = 0, raw_input=0;
	HANDLE_AACENCODER handle;
	CHANNEL_MODE mode;
	AACENC_InfoStruct info = { 0 };

	const struct option longopts[] = {
		{"bitrate",     required_argument,  0, 'b'},
	    {"input",       required_argument,  0, 'i'},
	    {"output",      required_argument,  0, 'o'},
	    {"format",      required_argument,  0, 'f'},
	    {"rate",        required_argument,  0, 'r'},
	    {"channels",    required_argument,  0, 'c'},
	    //{"lp",          no_argument,        0, 'l'},
	    //{"adts",        no_argument,        0, 't'},
	    {"afterburner", no_argument,        0, 'a'},
	    {"help",        no_argument,        0, 'h'},
	    {0,0,0,0},
	};

	int index;
	while(ch != -1) {
		ch = getopt_long(argc, argv, "tlhab:c:i:o:r:f:", longopts, &index);
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
		case 'o':
			outfile = optarg;
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

	if(outfile && strcmp(outfile, "-")) {
		out_fh = fopen(outfile, "wb");
		if(!out_fh) {
			fprintf(stderr, "Can't open output file!\n");
			return 1;
		}
	} else {
		out_fh = stdout;
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
		fprintf(stderr, "Unable to set the AOT\n");
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
	if (aacEncoder_SetParam(handle, AACENC_TRANSMUX, TT_DABPLUS) != AACENC_OK) {
		fprintf(stderr, "Unable to set the RAW transmux\n");
		return 1;
	}
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
	if (aacEncInfo(handle, &info) != AACENC_OK) {
		fprintf(stderr, "Unable to get the encoder info\n");
		return 1;
	}

	fprintf(stderr, "DAB+ Encoding: framelen=%d\n", info.frameLength);

	int input_size = channels*2*info.frameLength;
	input_buf = (uint8_t*) malloc(input_size);
	convert_buf = (int16_t*) malloc(input_size);
	rs_encoded_buf = (uint8_t*) malloc(120*subchannel_index);

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

	int frame=0;
	while (1) {
		AACENC_BufDesc in_buf = { 0 }, out_buf = { 0 };
		AACENC_InArgs in_args = { 0 };
		AACENC_OutArgs out_args = { 0 };
		int in_identifier = IN_AUDIO_DATA;
		int in_size, in_elem_size;
		int out_identifier = OUT_BITSTREAM_DATA;
		int out_size, out_elem_size;
		int read=0, i;
		void *in_ptr, *out_ptr;
		AACENC_ERROR err;

		if(raw_input) {
			if(fread(input_buf, input_size, 1, in_fh) == 1) {
				read = input_size;
			} else {
				fprintf(stderr, "Unable to read from input!\n");
				break;
			}
		} else {
			read = wav_read_data(wav, input_buf, input_size);
		}

		for (i = 0; i < read/2; i++) {
			const uint8_t* in = &input_buf[2*i];
			convert_buf[i] = in[0] | (in[1] << 8);
		}

		if (read <= 0) {
			in_args.numInSamples = -1;
		} else {
			in_ptr = convert_buf;
			in_size = read;
			in_elem_size = 2;

			in_args.numInSamples = read/2;
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
		char buf_to_rs_enc[110];
		char rs_enc[10];
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

		fwrite(outbuf, 1, /*out_args.numOutBytes*/ outbuf_size, out_fh);
		//fprintf(stderr, "Written %d/%d bytes!\n", out_args.numOutBytes + row*10, outbuf_size);
		if(out_args.numOutBytes + row*10 == outbuf_size)
			fprintf(stderr, ".");

//		if(frame > 10)
//			break;
		frame++;
	}
	free(input_buf);
	free(convert_buf);
	if(raw_input) {
		fclose(in_fh);
	} else {
		wav_read_close(wav);
	}
	fclose(out_fh);
	free_rs_char(rs_handler);

	aacEncClose(&handle);

	return 0;
}

