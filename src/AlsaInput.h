/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2013,2014 Matthias P. Braendli
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

#ifndef __ALSA_H_
#define __ALSA_H_
#include <cstdio>
#include <stdint.h>
#include <string>

#include <alsa/asoundlib.h>
#include <boost/thread/thread.hpp>

#include "SampleQueue.h"

using namespace std;

// 16 bits per sample is fine for now
#define BYTES_PER_SAMPLE 2

// How many samples we insert into the queue each call
#define NUM_SAMPLES_PER_CALL 10 // 10 samples @ 32kHz = 3.125ms

class AlsaInput
{
    public:
        AlsaInput(const string& alsa_dev,
                unsigned int channels,
                unsigned int rate,
                SampleQueue<uint8_t>& queue) :
            m_running(false),
            m_alsa_dev(alsa_dev),
            m_channels(channels),
            m_rate(rate),
            m_queue(queue),
            m_alsa_handle(NULL) { }

        ~AlsaInput()
        {
            if (m_running) {
                m_running = false;
                m_thread.interrupt();
                m_thread.join();
            }

            if (m_alsa_handle) {
                snd_pcm_abort(m_alsa_handle);
                m_alsa_handle = NULL;
            }
        }

        int prepare();

        int start();

    private:
        AlsaInput(const AlsaInput& other) : m_queue(other.m_queue) {}

        size_t read(uint8_t* buf, snd_pcm_uframes_t length);
        void process();

        bool m_running;
        boost::thread m_thread;
        string m_alsa_dev;
        unsigned int m_channels;
        unsigned int m_rate;

        SampleQueue<uint8_t>& m_queue;

        snd_pcm_t *m_alsa_handle;

};

#endif

