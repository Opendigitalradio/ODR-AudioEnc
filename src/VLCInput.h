/* ------------------------------------------------------------------
 * Copyright (C) 2015 Matthias P. Braendli
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
#include <vector>
#include <deque>
#include <thread>
#include <mutex>

#include <vlc/vlc.h>

#include "SampleQueue.h"

// 16 bits per sample is fine for now
#define BYTES_PER_SAMPLE 2

class VLCInput
{
    public:
        VLCInput(const std::string& uri,
                 int rate,
                 unsigned channels,
                 unsigned verbosity) :
            m_uri(uri),
            m_verbosity(verbosity),
            m_channels(channels),
            m_rate(rate),
            m_vlc(NULL) { }

        ~VLCInput() { cleanup(); }

        /* Prepare the audio input */
        int prepare();

        /* Read exactly length bytes into buf.
         * Blocks if not enough data is available,
         * or returns zero if EOF reached.
         *
         * Returns the number of bytes written into
         * the buffer.
         */
        ssize_t read(uint8_t* buf, size_t length);

        /* Write the last received ICY-Text to the
         * file.
         */
        void write_icy_text(const std::string& filename) const;

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

    protected:
        void cleanup(void);

        ssize_t m_read(uint8_t* buf, size_t length);

        std::vector<uint8_t> m_current_buf;

        mutable std::mutex m_queue_mutex;
        std::deque<uint8_t> m_queue;

        std::string m_uri;
        unsigned m_verbosity;
        unsigned m_channels;
        int m_rate;

        std::string m_nowplaying;

        // VLC pointers
        libvlc_instance_t     *m_vlc;
        libvlc_media_player_t *m_mp;

    private:
        VLCInput(const VLCInput& other) {}
};

#endif // HAVE_VLC

#endif

