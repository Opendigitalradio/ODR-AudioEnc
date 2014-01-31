/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2013 Matthias P. Braendli
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
"	  -b, --bitrate={ 8, 16, ..., 192 }    Output bitrate in kbps. Must be 8 multiple.\n"
//"	  -d, --data=FILENAME                  Set data filename.\n"
//"	  -g, --fs-bug                         Turn on FS bug mitigation.\n"
//"	  -i, --input=FILENAME                 Input filename (default: stdin).\n"
"	  -o, --output=URI                     Output zmq uri. (e.g. 'tcp://*:9000')\n"
"	  -a, --afterburner                    Turn on AAC encoder quality increaser.\n"
//"	  -m, --message                        Turn on AAC frame messages.\n"
//"	  -p, --pad=BYTES                      Set PAD size in bytes.\n"
//"	  -f, --format={ wav, raw }            Set input file format (default: wav).\n"
"	  -d, --device=alsa_device             Set ALSA input device (default: \"default\").\n"
"	  -c, --channels={ 1, 2 }              Nb of input channels for raw input (default: 2).\n"
"	  -r, --rate={ 32000, 48000 }          Sample rate for raw input (default: 48000).\n"
//"	  -t, --type=TYPE                      Set data type (dls|pad|packet|dg).\n"
//"	  -v, --verbose=LEVEL                  Set verbosity level.\n"
//"	  -V, --version                        Print version and exit.\n"
//"	  --mi=[ 0, ... ]                      Set AAC frame messages interval in milliseconds.\n"
//"	  --ma=[ 0, ... ]                      Set AAC frame messages attack time in milliseconds.\n"
//"	  -l, --lp                             Set frame size to 1024 instead of 960.\n"
"\n"
"Only the tcp:// zeromq transport has been tested until now.\n"

);

}

static int in_aborting = 0;
static snd_pcm_t *alsa_handle = NULL;

static void prg_exit(int code)
{
	if (alsa_handle)
		snd_pcm_close(alsa_handle);
	exit(code);
}

static void signal_handler(int sig)
{
	if (in_aborting)
		return;

	in_aborting = 1;
	if (alsa_handle)
		snd_pcm_abort(alsa_handle);

	if (sig == SIGABRT) {
		/* do not call snd_pcm_close() and abort immediately */
		alsa_handle = NULL;
		exit(EXIT_FAILURE);
	}
	signal(sig, signal_handler);
}

const static int dump_hw_params = 0;

// Set Alsa hardware parameters
static void set_params(void)
{
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t buffer_size;
	int err;
	size_t n;
	unsigned int rate;
	snd_pcm_uframes_t start_threshold, stop_threshold;
	snd_pcm_hw_params_alloca(&params);
	snd_pcm_sw_params_alloca(&swparams);
	err = snd_pcm_hw_params_any(alsa_handle, params);
	if (err < 0) {
		fprintf(stderr, "Broken configuration for this PCM: no configurations available");
		prg_exit(EXIT_FAILURE);
	}
	if (dump_hw_params) {
		fprintf(stderr, "HW Params of device \"%s\":\n",
			snd_pcm_name(alsa_handle));
		fprintf(stderr, "--------------------\n");
		// TODO log should be a snd_output_t *log;
		snd_pcm_hw_params_dump(params, log);
		fprintf(stderr, "--------------------\n");
	}
	err = snd_pcm_hw_params_set_access(alsa_handle, params,
			SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		fprintf(stderr, "Access type not available");
		prg_exit(EXIT_FAILURE);
	}
	err = snd_pcm_hw_params_set_format(alsa_handle, params, hwparams.format);
	if (err < 0) {
		fprintf(stderr, "Sample format non available");
		snd_pcm_format_t format;

		fprintf(stderr, "Available formats:\n");
		for (format = 0; format <= SND_PCM_FORMAT_LAST; format++) {
			if (snd_pcm_hw_params_test_format(alsa_handle, params, format) == 0)
				fprintf(stderr, "- %s\n", snd_pcm_format_name(format));
		}
		prg_exit(EXIT_FAILURE);
	}
	err = snd_pcm_hw_params_set_channels(alsa_handle, params, hwparams.channels);
	if (err < 0) {
		fprintf(stderr, "Channels count non available");
		prg_exit(EXIT_FAILURE);
	}

#if 0
	err = snd_pcm_hw_params_set_periods_min(alsa_handle, params, 2);
	assert(err >= 0);
#endif
	rate = hwparams.rate;
	err = snd_pcm_hw_params_set_rate_near(alsa_handle, params, &hwparams.rate, 0);
	assert(err >= 0);
	if ((float)rate * 1.05 < hwparams.rate || (float)rate * 0.95 > hwparams.rate) {
		char plugex[64];
		const char *pcmname = snd_pcm_name(alsa_handle);
		fprintf(stderr, "Warning: rate is not accurate (requested = %iHz, got = %iHz)\n", rate, hwparams.rate);
		if (! pcmname || strchr(snd_pcm_name(alsa_handle), ':')) {
			*plugex = 0;
		}
		else {
			snprintf(plugex, sizeof(plugex), "(-Dplug:%s)",
					snd_pcm_name(alsa_handle));
		}
		fprintf(stderr, "         please, try the plug plugin %s\n",
				plugex);
	}
	rate = hwparams.rate;
	if (buffer_time == 0 && buffer_frames == 0) {
		err = snd_pcm_hw_params_get_buffer_time_max(params,
							    &buffer_time, 0);
		assert(err >= 0);
		if (buffer_time > 500000)
			buffer_time = 500000;
	}
	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}
	if (period_time > 0)
		err = snd_pcm_hw_params_set_period_time_near(alsa_handle, params,
							     &period_time, 0);
	else
		err = snd_pcm_hw_params_set_period_size_near(alsa_handle, params,
							     &period_frames, 0);
	assert(err >= 0);
	if (buffer_time > 0) {
		err = snd_pcm_hw_params_set_buffer_time_near(alsa_handle, params,
							     &buffer_time, 0);
	} else {
		err = snd_pcm_hw_params_set_buffer_size_near(alsa_handle, params,
							     &buffer_frames);
	}
	assert(err >= 0);
	monotonic = snd_pcm_hw_params_is_monotonic(params);
	can_pause = snd_pcm_hw_params_can_pause(params);
	err = snd_pcm_hw_params(alsa_handle, params);
	if (err < 0) {
		fprintf(stderr, "Unable to install hw params:");
		snd_pcm_hw_params_dump(params, log);
		prg_exit(EXIT_FAILURE);
	}
	snd_pcm_hw_params_get_period_size(params, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (chunk_size == buffer_size) {
		fprintf(stderr, "Can't use period equal to buffer size (%lu == %lu)",
		      chunk_size, buffer_size);
		prg_exit(EXIT_FAILURE);
	}
	snd_pcm_sw_params_current(alsa_handle, swparams);
	if (avail_min < 0)
		n = chunk_size;
	else
		n = (double) rate * avail_min / 1000000;
	err = snd_pcm_sw_params_set_avail_min(alsa_handle, swparams, n);

	/* round up to closest transfer boundary */
	n = buffer_size;
	if (start_delay <= 0) {
		start_threshold = n + (double) rate * start_delay / 1000000;
	} else
		start_threshold = (double) rate * start_delay / 1000000;
	if (start_threshold < 1)
		start_threshold = 1;
	if (start_threshold > n)
		start_threshold = n;
	err = snd_pcm_sw_params_set_start_threshold(alsa_handle, swparams, start_threshold);
	assert(err >= 0);
	if (stop_delay <= 0) 
		stop_threshold = buffer_size + (double) rate * stop_delay / 1000000;
	else
		stop_threshold = (double) rate * stop_delay / 1000000;
	err = snd_pcm_sw_params_set_stop_threshold(alsa_handle, swparams, stop_threshold);
	assert(err >= 0);

	if (snd_pcm_sw_params(alsa_handle, swparams) < 0) {
		fprintf(stderr, "unable to install sw params:");
		snd_pcm_sw_params_dump(swparams, log);
		prg_exit(EXIT_FAILURE);
	}

	if (setup_chmap())
		prg_exit(EXIT_FAILURE);

	if (verbose)
		snd_pcm_dump(alsa_handle, log);

	bits_per_sample = snd_pcm_format_physical_width(hwparams.format);
	bits_per_frame = bits_per_sample * hwparams.channels;
	chunk_bytes = chunk_size * bits_per_frame / 8;
	audiobuf = realloc(audiobuf, chunk_bytes);
	if (audiobuf == NULL) {
		fprintf(stderr, "not enough memory");
		prg_exit(EXIT_FAILURE);
	}
	// fprintf(stderr, "real chunk_size = %i, frags = %i, total = %i\n", chunk_size, setup.buf.block.frags, setup.buf.block.frags * chunk_size);

	/* stereo VU-meter isn't always available... */
	if (vumeter == VUMETER_STEREO) {
		if (hwparams.channels != 2 || !interleaved || verbose > 2)
			vumeter = VUMETER_MONO;
	}

	/* show mmap buffer arragment */
	if (mmap_flag && verbose) {
		const snd_pcm_channel_area_t *areas;
		snd_pcm_uframes_t offset, size = chunk_size;
		int i;
		err = snd_pcm_mmap_begin(alsa_handle, &areas, &offset, &size);
		if (err < 0) {
			fprintf(stderr, "snd_pcm_mmap_begin problem: %s", snd_strerror(err));
			prg_exit(EXIT_FAILURE);
		}
		for (i = 0; i < hwparams.channels; i++)
			fprintf(stderr, "mmap_area[%i] = %p,%u,%u (%u)\n", i, areas[i].addr, areas[i].first, areas[i].step, snd_pcm_format_physical_width(hwparams.format));
		/* not required, but for sure */
		snd_pcm_mmap_commit(alsa_handle, offset, 0);
	}

	buffer_frames = buffer_size;	/* for position test */
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
	const int bits_per_sample = 16;
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


	const int open_mode = 0; //|= SND_PCM_NONBLOCK;
	const snd_pcm_stream_t stream = SND_PCM_STREAM_CAPTURE;
	const int nonblock = 0;
	snd_pcm_info_t *alsa_info;

	err = snd_pcm_open(&alsa_handle, alsa_device, stream, open_mode);
	if (err < 0) {
		fprintf(stderr, "audio open error: %s", snd_strerror(err));
		return 1;
	}

	if ((err = snd_pcm_info(alsa_handle, alsa_info)) < 0) {
		fprintf(stderr, "info error: %s", snd_strerror(err));
		prg_exit(1);
	}

	if (nonblock) {
		err = snd_pcm_nonblock(alsa_handle, 1);
		if (err < 0) {
			fprintf(stderr, "nonblock setting error: %s", snd_strerror(err));
			prg_exit(1);
		}
	}

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

	int input_size = channels*2*info.frameLength;
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
	fprintf(stderr, "outbuf_size: %d\n", outbuf_size);

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
		int read=0, i;
		int send_error;
		void *in_ptr, *out_ptr;
		AACENC_ERROR err;

		// raw input
		if(fread(input_buf, input_size, 1, in_fh) == 1) {
			read = input_size;
		} else {
			fprintf(stderr, "Unable to read from input!\n");
			break;
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

//		if(frame > 10)
//			break;
		frame++;
	}
	free(input_buf);
	free(convert_buf);

	zmq_close(zmq_sock);
	free_rs_char(rs_handler);

	aacEncClose(&handle);

	zmq_ctx_term(zmq_context);
	prg_exit(0);
}

