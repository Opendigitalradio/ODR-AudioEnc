/* ------------------------------------------------------------------
 * Copyright (C) 2016 Matthias P. Braendli
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

#ifndef __VLC_INPUT_H_
#define __VLC_INPUT_H_

#include "config.h"

#if HAVE_VLC

#include <cstdio>
#include <string>
#include <sstream>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <future>

#include <vlc/vlc.h>

#include "SampleQueue.h"
#include "common.h"

extern "C" {
#include "utils.h"
}

/* Common functionality for the direct libvlc input and the
 * threaded libvlc input
 */
class VLCInput
{
    public:
        VLCInput(const std::string& uri,
                 int rate,
                 unsigned channels,
                 unsigned verbosity,
                 std::string& gain,
                 std::string& cache,
                 std::vector<std::string>& additional_opts,
                 SampleQueue<uint8_t>& queue) :
            m_uri(uri),
            m_verbosity(verbosity),
            m_channels(channels),
            m_rate(rate),
            m_cache(cache),
            m_additional_opts(additional_opts),
            m_gain(gain),
            m_vlc(nullptr),
            m_mp(nullptr),
            m_fault(false),
            m_running(false),
            m_samplequeue(queue) {}

        VLCInput(const VLCInput& other) = delete;
        VLCInput& operator=(const VLCInput& other) = delete;
        ~VLCInput()
        {
            if (m_running) {
                m_running = false;
                m_thread.join();
            }

            cleanup();
        }

        /* Prepare the audio input */
        int prepare();

        /* Start the libVLC thread that fills the samplequeue */
        void start();

        /* Write the last received ICY-Text to the
         * file.
         */
        void write_icy_text(const std::string& filename, bool dl_plus);

        // Callbacks for VLC

        /* Notification of VLC exit */
        void exit_cb(void);

        /* Prepare a buffer for VLC */
        void preRender_cb(
                uint8_t** pp_pcm_buffer,
                size_t size);

        /* Notification from VLC that the buffer is now filled
         */
        void postRender_cb();

        int getRate() { return m_rate; }

        int getChannels() { return m_channels; }

        bool fault_detected() { return m_fault; };

    private:
        void cleanup(void);

        // Fill exactly length bytes into buf. Blocking.
        ssize_t m_read(uint8_t* buf, size_t length);

        std::vector<uint8_t> m_current_buf;

        std::mutex m_queue_mutex;
        std::deque<uint8_t> m_queue;

        std::string m_uri;
        unsigned m_verbosity;
        unsigned m_channels;
        int m_rate;

        // Whether to enable network caching in VLC or not
        std::string m_cache;

        // Given as-is to libvlc
        std::vector<std::string> m_additional_opts;

        // value for the VLC compressor filter
        std::string m_gain;


        std::future<bool> icy_text_written;
        std::mutex m_nowplaying_mutex;
        std::string m_nowplaying;
        std::string m_nowplaying_previous;

        // VLC pointers
        libvlc_instance_t     *m_vlc;
        libvlc_media_player_t *m_mp;

        // For the thread

        /* The function runnin in the thread */
        void process();

        std::atomic<bool> m_fault;
        std::atomic<bool> m_running;
        std::thread m_thread;
        SampleQueue<uint8_t>& m_samplequeue;
};

#endif // HAVE_VLC

#endif

