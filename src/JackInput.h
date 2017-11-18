/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
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
/*! \file JackInput.h
 *
 * This input uses JACK to get audio data. This always uses drift
 * compensation, because there is no blocking way to read from JACK.
 */

#pragma once

#include "config.h"
#include <cstdio>
#include <string>

#if HAVE_JACK

extern "C" {
#include <jack/jack.h>
}

#include "SampleQueue.h"
#include "InputInterface.h"

// 16 bits per sample is fine for now
#define BYTES_PER_SAMPLE 2

class JackInput : public InputInterface
{
    public:
        JackInput(const std::string& jack_name,
                unsigned int channels,
                unsigned int samplerate,
                SampleQueue<uint8_t>& queue) :
            m_client(NULL),
            m_jack_name(jack_name),
            m_channels(channels),
            m_rate(samplerate),
            m_queue(queue) { }

        JackInput(const JackInput& other) = delete;
        JackInput& operator=(const JackInput& other) = delete;

        virtual ~JackInput();

        virtual void prepare() override;

        virtual bool fault_detected(void) const override { return m_fault; };

        virtual bool read_source(size_t num_bytes) override;

    private:
        jack_client_t *m_client;

        std::vector<jack_port_t*> m_input_ports;

        std::string m_jack_name;
        unsigned int m_channels;
        unsigned int m_rate;

        // Callback for real-time JACK process
        void jack_process(jack_nframes_t nframes);

        // Callback when JACK shuts down
        void jack_shutdown()
        {
            m_fault = true;
        }

        bool m_fault = false;

        SampleQueue<uint8_t>& m_queue;

        // Static functions for JACK callbacks
        static int process_cb(jack_nframes_t nframes, void *arg)
        {
            ((JackInput*)arg)->jack_process(nframes);
            return 0;
        }

        static void shutdown_cb(void *arg)
        {
            ((JackInput*)arg)->jack_shutdown();
        }

};

#endif // HAVE_JACK

