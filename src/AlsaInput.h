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
/*! \file AlsaInput.h
 *
 * This input uses libasound to get audio data.
 */

#pragma once

#include "config.h"

#if HAVE_ALSA

#include <cstdio>
#include <string>
#include <thread>
#include <atomic>

#include <alsa/asoundlib.h>

#include "SampleQueue.h"
#include "common.h"
#include "InputInterface.h"

/*! Common functionality for the direct alsa input and the
 * threaded alsa input. The threaded one is used for
 * drift compensation.
 */
class AlsaInput : public InputInterface
{
    public:
        AlsaInput(const std::string& alsa_dev,
                unsigned int channels,
                unsigned int rate,
                SampleQueue<uint8_t>& queue) :
            m_alsa_dev(alsa_dev),
            m_channels(channels),
            m_rate(rate),
            m_queue(queue) { }

        AlsaInput(const AlsaInput& other) = delete;
        AlsaInput& operator=(const AlsaInput& other) = delete;

        virtual ~AlsaInput();

    protected:
        /* Read from the ALSA device. Returns number of samples,
         * or -1 in case of error
         */
        ssize_t m_read(uint8_t* buf, snd_pcm_uframes_t length);

        /* Open the ALSA device and set it up */
        void m_init_alsa(void);

        std::string m_alsa_dev;
        unsigned int m_channels;
        unsigned int m_rate;

        SampleQueue<uint8_t>& m_queue;

        snd_pcm_t *m_alsa_handle = nullptr;
};

class AlsaInputDirect : public AlsaInput
{
    public:
        AlsaInputDirect(const std::string& alsa_dev,
                unsigned int channels,
                unsigned int rate,
                SampleQueue<uint8_t>& queue) :
            AlsaInput(alsa_dev, channels, rate, queue) { }

        virtual void prepare(void) override;

        virtual bool fault_detected(void) const override { return false; };

        virtual bool read_source(size_t num_bytes) override;

        /*! Read length Bytes from from the alsa device.
         * length must be a multiple of channels * bytes_per_sample.
         *
         * \return the number of bytes read.
         */
        ssize_t read(uint8_t* buf, size_t length);
};

class AlsaInputThreaded : public AlsaInput
{
    public:
        AlsaInputThreaded(const std::string& alsa_dev,
                unsigned int channels,
                unsigned int rate,
                SampleQueue<uint8_t>& queue) :
            AlsaInput(alsa_dev, channels, rate, queue),
            m_fault(false),
            m_running(false) { }

        virtual ~AlsaInputThreaded();

        /*! Start the ALSA thread that fills the queue */
        virtual void prepare(void) override;

        virtual bool fault_detected(void) const override { return m_fault; };

        virtual bool read_source(size_t num_bytes) override;

    private:
        void process();

        std::atomic<bool> m_fault;
        std::atomic<bool> m_running;
        std::thread m_thread;

};

#endif // HAVE_ALSA

