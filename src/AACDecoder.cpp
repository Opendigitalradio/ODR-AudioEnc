/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2017 Matthias P. Braendli
 * Copyright (C) 2016 Stefan Pöschel
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

#include "config.h"
#include "AACDecoder.h"
#include <stdexcept>
#include <string>

AACDecoder::AACDecoder(const char* wavfilename) :
    m_wav_writer(wavfilename)
{
    m_handle = aacDecoder_Open(TT_MP4_RAW, 1);
    if (not m_handle) {
        throw std::runtime_error("AACDecoder: error opening decoder");
    }
}

void AACDecoder::decode_frame(uint8_t *data, size_t len)
{
    const bool dac_rate             = data[2] & 0x40;
    const bool sbr_flag             = data[2] & 0x20;
    const bool aac_channel_mode     = data[2] & 0x10;
    const bool ps_flag              = data[2] & 0x08;
    //const uint8_t mpeg_surround_config = data[2] & 0x07;

    const int core_sr_index = dac_rate ?
        (sbr_flag ? 6 : 3) : (sbr_flag ? 8 : 5);   // 24/48/16/32 kHz
    const int core_ch_config = aac_channel_mode ? 2 : 1;
    const int extension_sr_index = dac_rate ? 3 : 5;    // 48/32 kHz

    int au_start[6] = {};

    int num_aus = dac_rate ? (sbr_flag ? 3 : 6) : (sbr_flag ? 2 : 4);
    au_start[0] = dac_rate ? (sbr_flag ? 6 : 11) : (sbr_flag ? 5 : 8);
    au_start[1] = data[3] << 4 | data[4] >> 4;

    if (num_aus >= 3) {
        au_start[2] = (data[4] & 0x0F) << 8 | data[5];
    }

    if (num_aus >= 4) {
        au_start[3] = data[6] << 4 | data[7] >> 4;
    }

    if (num_aus == 6) {
        au_start[4] = (data[7] & 0x0F) << 8 | data[8];
        au_start[5] = data[9] << 4 | data[10] >> 4;
    }

	au_start[num_aus] = len; // end of the buffer

    for (int i = 0; i < num_aus; i++) {
        if (au_start[i] >= au_start[i+1]) {
            throw std::runtime_error("  AU ordering check failed\n");
        }
    }

    if (not m_decoder_set_up) {
        std::vector<uint8_t> asc;

        // AAC LC
        asc.push_back(0b00010 << 3 | core_sr_index >> 1);
        asc.push_back((core_sr_index & 0x01) << 7 | core_ch_config << 3 | 0b100);

        if (sbr_flag) {
            // add SBR
            asc.push_back(0x56);
            asc.push_back(0xE5);
            asc.push_back(0x80 | (extension_sr_index << 3));

            if (ps_flag) {
                // add PS
                asc.back() |= 0x05;
                asc.push_back(0x48);
                asc.push_back(0x80);
            }
        }

        uint8_t* asc_array[1] {asc.data()};
        const unsigned int asc_sizeof_array[1] {(unsigned int) asc.size()};

        AAC_DECODER_ERROR init_result = aacDecoder_ConfigRaw(m_handle,
                asc_array, asc_sizeof_array);
        if (init_result != AAC_DEC_OK) {
            throw std::runtime_error(
                    "AACDecoderFDKAAC: error while aacDecoder_ConfigRaw: " +
                    std::to_string(init_result));
        }

        m_channels = (aac_channel_mode or ps_flag) ? 2 : 1;
        size_t output_frame_len = 960 * 2 * m_channels * (sbr_flag ? 2 : 1);
        m_output_frame.resize(output_frame_len);
        fprintf(stderr, "  Setting decoder output frame len %zu\n", output_frame_len);

        const int sample_rate = dac_rate ? 48000 : 32000;
        m_wav_writer.initialise_header(sample_rate, m_channels);
        m_decoder_set_up = true;

        fprintf(stderr, "  Set up decoder with %d Hz, %s%swith %d channels\n",
                sample_rate, (sbr_flag ? "SBR " : ""), (ps_flag ? "PS " : ""),
                m_channels);

    }

    const size_t AU_CRCLEN = 2;
    for (int i = 0; i < num_aus; i++) {
        uint8_t *au_data = data + au_start[i];
        size_t au_len = au_start[i+1] - au_start[i] - AU_CRCLEN;
        decode_au(au_data, au_len);
    }
}

void AACDecoder::decode_au(uint8_t *data, size_t len)
{
    uint8_t* input_buffer[1] {data};
    const unsigned int input_buffer_size[1] {(unsigned int) len};
    unsigned int bytes_valid = len;

    // fill internal input buffer
    AAC_DECODER_ERROR result = aacDecoder_Fill(
            m_handle, input_buffer, input_buffer_size, &bytes_valid);

    if (result != AAC_DEC_OK) {
        throw std::runtime_error(
                "AACDecoderFDKAAC: error while aacDecoder_Fill: " +
                std::to_string(result));
    }

    if (bytes_valid) {
        throw std::runtime_error(
                "AACDecoderFDKAAC: aacDecoder_Fill did not consume all bytes");
    }

    // decode audio
    result = aacDecoder_DecodeFrame(m_handle,
            (short int*)m_output_frame.data(), m_output_frame.size(), 0);
    if (result != AAC_DEC_OK) {
        throw std::runtime_error(
                "AACDecoderFDKAAC: error while aacDecoder_DecodeFrame: " +
                std::to_string(result));
    }

    m_wav_writer.write_data(m_output_frame.data(), m_output_frame.size());
}
