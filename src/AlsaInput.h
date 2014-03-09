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
                unsigned int rate) :
            m_alsa_dev(alsa_dev),
            m_channels(channels),
            m_rate(rate),
            m_alsa_handle(NULL) { }

        ~AlsaInput() {

            if (m_alsa_handle) {
                snd_pcm_abort(m_alsa_handle);
                m_alsa_handle = NULL;
            }
        }

        /* Prepare the audio input */
        int prepare();

        virtual void start() = 0;

    protected:
        size_t m_read(uint8_t* buf, snd_pcm_uframes_t length);

        string m_alsa_dev;
        unsigned int m_channels;
        unsigned int m_rate;

        snd_pcm_t *m_alsa_handle;

    private:
        AlsaInput(const AlsaInput& other) {}
};

class AlsaInputDirect : public AlsaInput
{
    public:
        AlsaInputDirect(const string& alsa_dev,
                unsigned int channels,
                unsigned int rate) :
            AlsaInput(alsa_dev, channels, rate) { }

        virtual void start() { };

        /* Read length Bytes from from the alsa device.
         * length must be a multiple of channels * bytes_per_sample.
         *
         * Returns the number of bytes read.
         */
        size_t read(uint8_t* buf, size_t length);

    private:
        AlsaInputDirect(const AlsaInputDirect& other) :
            AlsaInput("", 0, 0) { }
};

class AlsaInputThreaded : public AlsaInput
{
    public:
        AlsaInputThreaded(const string& alsa_dev,
                unsigned int channels,
                unsigned int rate,
                SampleQueue<uint8_t>& queue) :
            AlsaInput(alsa_dev, channels, rate),
            m_running(false),
            m_queue(queue) { }

        ~AlsaInputThreaded()
        {
            if (m_running) {
                m_running = false;
                m_thread.interrupt();
                m_thread.join();
            }
        }

        virtual void start();

    private:
        AlsaInputThreaded(const AlsaInputThreaded& other) :
            AlsaInput("", 0, 0),
            m_queue(other.m_queue) {}

        void process();

        bool m_running;
        boost::thread m_thread;

        SampleQueue<uint8_t>& m_queue;

};

#endif

