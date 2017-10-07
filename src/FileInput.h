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
/*! \section File Input
 *
 * This input reads a wav or raw file.
 *
 * The raw input needs to be signed 16-bit per sample data, with
 * the number of channels corresponding to the command line.
 *
 * The wav input must also correspond to the parameters on the command
 * line (number of channels, rate)
 */

#pragma once

#include <stdint.h>
#include <cstdio>
#include <string>
#include "SampleQueue.h"
#include "InputInterface.h"

class FileInput : public InputInterface
{
    public:
        FileInput(const std::string& filename,
                bool raw_input,
                int sample_rate,
                bool continue_after_eof,
                SampleQueue<uint8_t>& queue) :
            m_filename(filename),
            m_raw_input(raw_input),
            m_sample_rate(sample_rate),
            m_continue_after_eof(continue_after_eof),
            m_queue(queue) {}

        virtual ~FileInput();
        FileInput(const FileInput& other) = delete;
        FileInput& operator=(const FileInput& other) = delete;

        /*! Open the file and prepare the wav decoder. */
        virtual void prepare(void) override;

        virtual bool fault_detected(void) const override { return false; };

        virtual bool read_source(size_t num_bytes) override;

    protected:
        std::string m_filename;
        bool m_raw_input;
        int m_sample_rate;
        bool m_continue_after_eof;
        SampleQueue<uint8_t>& m_queue;

        /* handle to the wav reader */
        void *m_wav = nullptr;
        FILE* m_in_fh = nullptr;
};

