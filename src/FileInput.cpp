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
    // Ensures push() doesn't get blocked
    m_queue.clear();

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

bool FileInput::read_source(size_t num_bytes)
{
    vector<uint8_t> samplebuf(num_bytes);

    ssize_t ret = 0;

    if (m_raw_input) {
        ret = fread(samplebuf.data(), 1, num_bytes, m_in_fh);
    }
    else {
        ret = wav_read_data(m_wav, samplebuf.data(), num_bytes);
    }

    if (ret > 0) {
        m_queue.push(samplebuf.data(), ret);
    }

    if (ret < (ssize_t)num_bytes) {
        if (m_raw_input) {
            if (ferror(m_in_fh)) {
                return false;
            }

            if (feof(m_in_fh)) {
                if (m_continue_after_eof) {
                    clearerr(m_in_fh);
                }
                else {
                    return false;
                }
            }
        }
        else {
            // the wavfile input doesn't support the continuation after EOF
            return false;
        }
    }

    return true;
}

