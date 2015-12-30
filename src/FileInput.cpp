/* ------------------------------------------------------------------
 * Copyright (C) 2014 Matthias P. Braendli
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

#include "FileInput.h"
#include "wavreader.h"
#include <cstring>
#include <cstdio>

#include <stdint.h>

int FileInput::prepare(void)
{
    if (m_raw_input) {
        if (m_filename && strcmp(m_filename, "-")) {
            m_in_fh = fopen(m_filename, "rb");
            if (!m_in_fh) {
                fprintf(stderr, "Can't open input file!\n");
                return 1;
            }
        }
        else {
            m_in_fh = stdin;
        }
    }
    else {
        int bits_per_sample = 0;
        int channels = 0;
        int wav_format = 0;
        int sample_rate = 0;

        m_wav = wav_read_open(m_filename);
        if (!m_wav) {
            fprintf(stderr, "Unable to open wav file %s\n", m_filename);
            return 1;
        }
        if (!wav_get_header(m_wav, &wav_format, &channels, &sample_rate,
                    &bits_per_sample, NULL)) {
            fprintf(stderr, "Bad wav file %s\n", m_filename);
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
        if ( !(channels == 1 or channels == 2)) {
            fprintf(stderr, "Unsupported WAV channels %d\n", channels);
            return 1;
        }
        if (m_sample_rate != sample_rate) {
            fprintf(stderr,
                    "WAV sample rate %d doesn't correspond to desired sample rate %d\n",
                    sample_rate, m_sample_rate);
            return 1;
        }
    }

    return 0;
}

ssize_t FileInput::read(uint8_t* buf, size_t length)
{
    ssize_t pcmread;

    if (m_raw_input) {
        if (fread(buf, length, 1, m_in_fh) == 1) {
            pcmread = length;
        }
        else {
            //fprintf(stderr, "Unable to read from input!\n");
            return 0;
        }
    }
    else {
        pcmread = wav_read_data(m_wav, buf, length);
    }

    return pcmread;
}

int FileInput::eof()
{
    int eof = feof(m_in_fh);
    clearerr(m_in_fh);
    return eof;
}


FileInput::~FileInput()
{
    if (m_raw_input && m_in_fh) {
        fclose(m_in_fh);
    }
    else if (m_wav) {
        wav_read_close(m_wav);
    }
}

