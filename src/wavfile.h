/* ------------------------------------------------------------------
 * Copyright (C) 2009 Martin Storsjo
 * Copyright (C) 2018 Matthias P. Braendli
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

#pragma once

#include <cstdio>
#include <cstdint>

void* wav_read_open(const char *filename);
void wav_read_close(void* obj);

int wav_get_header(void* obj, int* format, int* channels, int* sample_rate, int* bits_per_sample, unsigned int* data_length);
int wav_read_data(void* obj, unsigned char* data, unsigned int length);

class WavWriter {
    public:
        WavWriter(const char *filename);
        ~WavWriter();
        WavWriter(const WavWriter& other) = delete;
        WavWriter& operator=(const WavWriter& other) = delete;

        void initialise_header(int rate, int channels);

        void write_data(const uint8_t *data, int length);

    private:
        FILE *m_fd = nullptr;
};

