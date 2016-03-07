#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "encoder.h"
#include "options.h"
#include "bitstream.h"
#include "mem.h"
#include "crc.h"
#include "psycho_n1.h"
#include "psycho_0.h"
#include "psycho_1.h"
#include "psycho_2.h"
#include "psycho_3.h"
#include "psycho_4.h"
#include "encode.h"
#include "availbits.h"
#include "subband.h"
#include "encode_new.h"
#include "toolame.h"
#include "utils.h"
#include <assert.h>

Bit_stream_struc bs;

const int FPAD_LENGTH=2;

void smr_dump(double smr[2][SBLIMIT], int nch);

void global_init (void)
{
    glopts.usepsy = TRUE;
    glopts.usepadbit = TRUE;
    glopts.quickmode = FALSE;
    glopts.quickcount = 10;
    glopts.downmix = FALSE;
    glopts.byteswap = FALSE;
    glopts.vbr = FALSE;
    glopts.vbrlevel = 0;
    glopts.athlevel = 0;
    glopts.verbosity = 2;
}

/************************************************************************
 *
 * main
 *
 * PURPOSE:  MPEG II Encoder with
 * psychoacoustic models 1 (MUSICAM) and 2 (AT&T)
 *
 * SEMANTICS:  One overlapping frame of audio of up to 2 channels are
 * processed at a time in the following order:
 * (associated routines are in parentheses)
 *
 * 1.  Filter sliding window of data to get 32 subband
 * samples per channel.
 * (window_subband,filter_subband)
 *
 * 2.  If joint stereo mode, combine left and right channels
 * for subbands above #jsbound#.
 * (combine_LR)
 *
 * 3.  Calculate scalefactors for the frame, and 
 * also calculate scalefactor select information.
 * (*_scale_factor_calc)
 *
 * 4.  Calculate psychoacoustic masking levels using selected
 * psychoacoustic model.
 * (psycho_i, psycho_ii)
 *
 * 5.  Perform iterative bit allocation for subbands with low
 * mask_to_noise ratios using masking levels from step 4.
 * (*_main_bit_allocation)
 *
 * 6.  If error protection flag is active, add redundancy for
 * error protection.
 * (*_CRC_calc)
 *
 * 7.  Pack bit allocation, scalefactors, and scalefactor select
 *headerrmation onto bitstream.
 * (*_encode_bit_alloc,*_encode_scale,transmission_pattern)
 *
 * 8.  Quantize subbands and pack them into bitstream
 * (*_subband_quantization, *_sample_encoding)
 *
 ************************************************************************/

static frame_info frame;
static frame_header header;
static int frameNum;
static int xpad_len;
static int psycount;
static int model;
static unsigned int crc;

typedef double SBS[2][3][SCALE_BLOCK][SBLIMIT];
typedef double JSBS[3][SCALE_BLOCK][SBLIMIT];
typedef unsigned int SUB[2][3][SCALE_BLOCK][SBLIMIT];

static SBS *sb_sample;
static JSBS *j_sample;
static SUB *subband;

static unsigned int scalar[2][3][SBLIMIT];
static unsigned int j_scale[3][SBLIMIT];

static double smr[2][SBLIMIT];
static double max_sc[2][SBLIMIT];
static short sam[2][1344];

/* Used to keep the SNR values for the fast/quick psy models */
static FLOAT smrdef[2][32];

static unsigned int scfsi[2][SBLIMIT];
static unsigned int bit_alloc[2][SBLIMIT];

static char* mot_file = NULL;
static char* icy_file = NULL;

int toolame_init(void)
{
    frameNum = 0;
    psycount = 0;

    frame.header = &header;
    frame.tab_num = -1;		/* no table loaded */
    frame.alloc = NULL;

    sb_sample = (SBS *) mem_alloc (sizeof (SBS), "sb_sample");
    j_sample = (JSBS *) mem_alloc (sizeof (JSBS), "j_sample");
    subband = (SUB *) mem_alloc (sizeof (SUB), "subband");
    memset ((char *) scalar, 0, sizeof (scalar));
    memset ((char *) j_scale, 0, sizeof (j_scale));
    memset ((char *) smr, 0, sizeof (smr));
    memset ((char *) max_sc, 0, sizeof (max_sc));
    memset ((char *) sam, 0, sizeof (sam));
    memset ((char *) scfsi, 0, sizeof (scfsi));
    memset ((char *) bit_alloc, 0, sizeof (bit_alloc));

    global_init();

    header.extension = 0;
    header.version = MPEG_AUDIO_ID;	/* Default: MPEG-1 */
    header.copyright = 0;
    header.original = 0;
    header.error_protection = TRUE;
    header.dab_extension = 4;
    header.lay = DFLT_LAY;

    model = DFLT_PSY;

    return 0;
}

int toolame_finish(
        unsigned char *output_buffer,
        size_t output_buffer_size)
{
    bs.output_buffer = output_buffer;
    bs.output_buffer_size = output_buffer_size;
    bs.output_buffer_written = 0;

    close_bit_stream_w(&bs);

    return bs.output_buffer_written;
}

int toolame_enable_downmix_stereo(void)
{
    glopts.downmix = TRUE;
    header.mode = MPG_MD_MONO;
    header.mode_ext = 0;
    return 0;
}

int toolame_enable_byteswap(void)
{
    glopts.byteswap = TRUE;
    return 0;
}

int toolame_set_channel_mode(const char mode)
{
    switch (mode) {
        case 's':
            header.mode = MPG_MD_STEREO;
            header.mode_ext = 0;
            break;
        case 'd':
            header.mode = MPG_MD_DUAL_CHANNEL;
            header.mode_ext = 0;
            break;
            /* in j-stereo mode, no default header.mode_ext was defined, gave error..
               now  default = 2   added by MFC 14 Dec 1999.  */
        case 'j':
            header.mode = MPG_MD_JOINT_STEREO;
            header.mode_ext = 2;
            break;
        case 'm':
            header.mode = MPG_MD_MONO;
            header.mode_ext = 0;
            break;
        default:
            fprintf (stderr, "libtoolame-dab: Bad mode %c\n", mode);
            return 1;
    }
    return 0;
}

int toolame_set_psy_model(int new_model)
{
    if (new_model < 0 || new_model > 3) {
        fprintf(stderr, "libtoolame-dab: Invalid PSY model %d\n", new_model);
        return 1;
    }
    model = new_model;
    return 0;
}

int toolame_set_bitrate(int brate)
{
    int err = 0;

    /* check for a valid bitrate */
    if (brate == 0)
        brate = bitrate[header.version][10];

    /* Check to see we have a sane value for the bitrate for this version */
    if ((header.bitrate_index = BitrateIndex (brate, header.version)) < 0) {
        err = 1;
    }

    if (header.dab_extension) {
        /* in 48 kHz (= MPEG-1) */
        /* if the bit rate per channel is less then 56 kbit/s, we have 2 scf-crc */
        /* else we have 4 scf-crc */
        /* in 24 kHz (= MPEG-2), we have 4 scf-crc */
        if (header.version == MPEG_AUDIO_ID && (brate / (header.mode == MPG_MD_MONO ? 1 : 2) < 56))
            header.dab_extension = 2;
    }

    open_bit_stream_w(&bs, BUFFER_SIZE);

    return err;
}

int toolame_set_samplerate(long sample_rate)
{
    int s_freq = SmpFrqIndex(sample_rate, &header.version);
    if (s_freq < 0) {
        return s_freq;
    }

    header.sampling_frequency = s_freq;
    return 0;
}

int toolame_set_pad(int pad_len)
{
    header.dab_length = pad_len;
    if (header.dab_length <= 0) {
        fprintf(stderr, "Invalid XPAD length specified\n");
        return 1;
    }

    return 0;
}


static int encode_first_call = 1;

int toolame_encode_frame(
        short buffer[2][1152],
        unsigned char *xpad_data,
        unsigned char *output_buffer,
        size_t output_buffer_size)
{
    extern int minimum;
    if (encode_first_call) {
        hdr_to_frps(&frame);
        encode_first_call = 0;
    }

    const int nch = frame.nch;
    const int error_protection = header.error_protection;

    bs.output_buffer = output_buffer;
    bs.output_buffer_size = output_buffer_size;
    bs.output_buffer_written = 0;

#ifdef REFERENCECODE
    short *win_buf[2] = {&buffer[0][0], &buffer[1][0]};
#endif

    int adb = available_bits (&header, &glopts);
    int lg_frame = adb / 8;
    if (header.dab_extension) {
        /* You must have one frame in memory if you are in DAB mode                 */
        /* in conformity of the norme ETS 300 401 http://www.etsi.org               */
        /* see bitstream.c            */
        if (frameNum == 1)
            minimum = lg_frame + MINIMUM;
        adb -= header.dab_extension * 8 + (xpad_len ? xpad_len : FPAD_LENGTH) * 8;
    }

    {
        int gr, bl, ch;
        /* New polyphase filter
           Combines windowing and filtering. Ricardo Feb'03 */
        for( gr = 0; gr < 3; gr++ )
            for ( bl = 0; bl < 12; bl++ )
                for ( ch = 0; ch < nch; ch++ )
                    WindowFilterSubband( &buffer[ch][gr * 12 * 32 + 32 * bl], ch,
                            &(*sb_sample)[ch][gr][bl][0] );
    }

#ifdef REFERENCECODE
    {
        /* Old code. left here for reference */
        int gr, bl, ch;
        for (gr = 0; gr < 3; gr++)
            for (bl = 0; bl < SCALE_BLOCK; bl++)
                for (ch = 0; ch < nch; ch++) {
                    window_subband (&win_buf[ch], &(*win_que)[ch][0], ch);
                    filter_subband (&(*win_que)[ch][0], &(*sb_sample)[ch][gr][bl][0]);
                }
    }
#endif


#ifdef NEWENCODE
    scalefactor_calc_new(*sb_sample, scalar, nch, frame.sblimit);
    find_sf_max (scalar, &frame, max_sc);
    if (frame.actual_mode == MPG_MD_JOINT_STEREO) {
        /* this way we calculate more mono than we need */
        /* but it is cheap */
        combine_LR_new (*sb_sample, *j_sample, frame.sblimit);
        scalefactor_calc_new (j_sample, &j_scale, 1, frame.sblimit);
    }
#else
    scale_factor_calc (*sb_sample, scalar, nch, frame.sblimit);
    pick_scale (scalar, &frame, max_sc);
    if (frame.actual_mode == MPG_MD_JOINT_STEREO) {
        /* this way we calculate more mono than we need */
        /* but it is cheap */
        combine_LR (*sb_sample, *j_sample, frame.sblimit);
        scale_factor_calc (j_sample, &j_scale, 1, frame.sblimit);
    }
#endif



    if ((glopts.quickmode == TRUE) && (++psycount % glopts.quickcount != 0)) {
        /* We're using quick mode, so we're only calculating the model every
           'quickcount' frames. Otherwise, just copy the old ones across */
        for (int ch = 0; ch < nch; ch++) {
            for (int sb = 0; sb < SBLIMIT; sb++)
                smr[ch][sb] = smrdef[ch][sb];
        }
    }
    else {
        /* calculate the psymodel */
        switch (model) {
            case -1:
                psycho_n1 (smr, nch);
                break;
            case 0:	/* Psy Model A */
                psycho_0 (smr, nch, scalar, (FLOAT) s_freq[header.version][header.sampling_frequency] * 1000);
                break;
            case 1:
                psycho_1 (buffer, max_sc, smr, &frame);
                break;
            case 2:
                for (int ch = 0; ch < nch; ch++) {
                    psycho_2 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], //snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                }
                break;
            case 3:
                /* Modified psy model 1 */
                psycho_3 (buffer, max_sc, smr, &frame, &glopts);
                break;
            case 4:
                /* Modified Psycho Model 2 */
                for (int ch = 0; ch < nch; ch++) {
                    psycho_4 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], // snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                }
                break;	
            case 5:
                /* Model 5 comparse model 1 and 3 */
                psycho_1 (buffer, max_sc, smr, &frame);
                fprintf(stdout,"1 ");
                smr_dump(smr,nch);
                psycho_3 (buffer, max_sc, smr, &frame, &glopts);
                fprintf(stdout,"3 ");
                smr_dump(smr,nch);
                break;
            case 6:
                /* Model 6 compares model 2 and 4 */
                for (int ch = 0; ch < nch; ch++) 
                    psycho_2 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], //snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                fprintf(stdout,"2 ");
                smr_dump(smr,nch);
                for (int ch = 0; ch < nch; ch++) 
                    psycho_4 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], // snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                fprintf(stdout,"4 ");
                smr_dump(smr,nch);
                break;
            case 7:
                fprintf(stdout,"Frame: %i\n",frameNum);
                /* Dump the SMRs for all models */	
                psycho_1 (buffer, max_sc, smr, &frame);
                fprintf(stdout,"1");
                smr_dump(smr, nch);
                psycho_3 (buffer, max_sc, smr, &frame, &glopts);
                fprintf(stdout,"3");
                smr_dump(smr,nch);
                for (int ch = 0; ch < nch; ch++) 
                    psycho_2 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], //snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                fprintf(stdout,"2");
                smr_dump(smr,nch);
                for (int ch = 0; ch < nch; ch++) 
                    psycho_4 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], // snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                fprintf(stdout,"4");
                smr_dump(smr,nch);
                break;
            case 8:
                /* Compare 0 and 4 */	
                psycho_n1 (smr, nch);
                fprintf(stdout,"0");
                smr_dump(smr,nch);

                for (int ch = 0; ch < nch; ch++) 
                    psycho_4 (&buffer[ch][0], &sam[ch][0], ch, &smr[ch][0], // snr32,
                            (FLOAT) s_freq[header.version][header.sampling_frequency] *
                            1000, &glopts);
                fprintf(stdout,"4");
                smr_dump(smr,nch);
                break;
            default:
                fprintf (stderr, "Invalid psy model specification: %i\n", model);
                exit (0);
        }

        if (glopts.quickmode == TRUE) {
            /* copy the smr values and reuse them later */
            for (int ch = 0; ch < nch; ch++) {
                for (int sb = 0; sb < SBLIMIT; sb++)
                    smrdef[ch][sb] = smr[ch][sb];
            }
        }

        if (glopts.verbosity > 4) {
            smr_dump(smr, nch);
        }
    }

#ifdef NEWENCODE
    sf_transmission_pattern (scalar, scfsi, &frame);
    main_bit_allocation_new (smr, scfsi, bit_alloc, &adb, &frame, &glopts);
    //main_bit_allocation (smr, scfsi, bit_alloc, &adb, &frame, &glopts);

    if (error_protection) {
        CRC_calc (&frame, bit_alloc, scfsi, &crc);
    }

    write_header (&frame, &bs);
    //encode_info (&frame, &bs);
    if (error_protection) {
        putbits (&bs, crc, 16);
    }
    write_bit_alloc (bit_alloc, &frame, &bs);
    //encode_bit_alloc (bit_alloc, &frame, &bs);
    write_scalefactors(bit_alloc, scfsi, scalar, &frame, &bs);
    //encode_scale (bit_alloc, scfsi, scalar, &frame, &bs);
    subband_quantization_new (scalar, *sb_sample, j_scale, *j_sample, bit_alloc,
            *subband, &frame);
    //subband_quantization (scalar, *sb_sample, j_scale, *j_sample, bit_alloc,
    //	  *subband, &frame);
    write_samples_new(*subband, bit_alloc, &frame, &bs);
    //sample_encoding (*subband, bit_alloc, &frame, &bs);
#else
    transmission_pattern (scalar, scfsi, &frame);
    main_bit_allocation (smr, scfsi, bit_alloc, &adb, &frame, &glopts);
    if (error_protection) {
        CRC_calc (&frame, bit_alloc, scfsi, &crc);
    }
    encode_info (&frame, &bs);
    if (error_protection) {
        encode_CRC (crc, &bs);
    }
    encode_bit_alloc (bit_alloc, &frame, &bs);
    encode_scale (bit_alloc, scfsi, scalar, &frame, &bs);
    subband_quantization (scalar, *sb_sample, j_scale, *j_sample, bit_alloc,
            *subband, &frame);
    sample_encoding (*subband, bit_alloc, &frame, &bs);
#endif


    /* If not all the bits were used, write out a stack of zeros */
    for (int i = 0; i < adb; i++) {
        put1bit (&bs, 0);
    }


    if (xpad_len) {
        assert(xpad_len > 2);

        // insert available X-PAD
        for (int i = header.dab_length - xpad_len;
                i < header.dab_length - FPAD_LENGTH;
                i++) {
            putbits (&bs, xpad_data[i], 8);
        }
    }


    for (int i = header.dab_extension - 1; i >= 0; i--) {
        CRC_calcDAB (&frame, bit_alloc, scfsi, scalar, &crc, i);
        /* this crc is for the previous frame in DAB mode  */
        if (bs.buf_byte_idx + lg_frame < bs.buf_size)
            bs.buf[bs.buf_byte_idx + lg_frame] = crc;
        /* reserved 2 bytes for F-PAD in DAB mode  */
        putbits (&bs, crc, 8);
    }

    if (xpad_len) {
        /* The F-PAD is also given us by mot-encoder */
        putbits (&bs, xpad_data[header.dab_length - 2], 8);
        putbits (&bs, xpad_data[header.dab_length - 1], 8);
    }
    else {
        putbits (&bs, 0, 16); // FPAD is all-zero
    }

    return bs.output_buffer_written;
}

// Dump function for psy model comparison
void smr_dump(double smr[2][SBLIMIT], int nch)
{
    fprintf(stdout, "SMR:");

    for (int ch = 0; ch < nch; ch++) {
        if (ch==1) {
            fprintf(stdout, "    ");
        }
        for (int sb = 0; sb < SBLIMIT; sb++) {
            fprintf(stdout, "%3.0f ", smr[ch][sb]);
        }
        fprintf(stdout, "\n");
    }
}

