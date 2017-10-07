/* ------------------------------------------------------------------
 * Copyright (C) 2017 Matthias P. Braendli
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
#include "wavfile.h"
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <stdint.h>

using namespace std;

FileInput::~FileInput()
{
    if (m_raw_input and m_in_fh) {
        fclose(m_in_fh);
    }
    else if (m_wav) {
        wav_read_close(m_wav);
    }
}

void FileInput::prepare(void)
{
    const char* fname = m_filename.c_str();

    if (m_raw_input) {
        if (fname && strcmp(fname, "-")) {
            m_in_fh = fopen(fname, "rb");
            if (!m_in_fh) {
                throw runtime_error("Can't open input file!");
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

        m_wav = wav_read_open(fname);
        if (!m_wav) {
            throw runtime_error("Unable to open wav file " + m_filename);
        }
        if (!wav_get_header(m_wav, &wav_format, &channels, &sample_rate,
                    &bits_per_sample, nullptr)) {
            throw runtime_error("Bad wav file" + m_filename);
        }
        if (wav_format != 1) {
            throw runtime_error("Unsupported WAV format " + to_string(wav_format));
        }
        if (bits_per_sample != 16) {
            throw runtime_error("Unsupported WAV sample depth " +
                    to_string(bits_per_sample));
        }
        if ( !(channels == 1 or channels == 2)) {
            throw runtime_error("Unsupported WAV channels " + to_string(channels));
        }
        if (m_sample_rate != sample_rate) {
            throw runtime_error(
                    "WAV sample rate " +
                    to_string(sample_rate) +
                    " doesn't correspond to desired sample rate " +
                    to_string(m_sample_rate));
        }
    }
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


