/* ------------------------------------------------------------------
 * Copyright (C) 2019 Matthias P. Braendli
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

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>

/*! \file StatsPublish.h
 *
 * Collects and sends some stats to a UNIX DGRAM socket so that an external tool
 * like ODR-EncoderManager can display it.
 *
 * Currently, only audio levels are collected.
 *
 * Output is formatted in YAML
 */
class StatsPublisher {
    public:
        StatsPublisher(const std::string& socket_path);
        StatsPublisher(const StatsPublisher& other) = delete;
        StatsPublisher& operator=(const StatsPublisher& other) = delete;
        ~StatsPublisher();

        /*! Update peak audio level information */
        void update_audio_levels(int16_t audiolevel_left, int16_t audiolevel_right);

        /*! Update decoded AAC frame. Audio is in S16 interleaved raw format */
        void update_decoded_audio(const std::vector<uint8_t>& audio, int samplerate, int channels);

        /*! Increments the underrun counter */
        void notify_underrun();

        /*! Increments the overrun counter */
        void notify_overrun();

        /*! Send the collected stats to the socket, doesn't block. If the socket is
         * not connected, the data is lost.
         *
         * Clears the collected data.  */
        void send_stats();

    private:
        std::string m_socket_path;
        int m_sock = -1;

        int16_t m_audio_left = 0;
        int16_t m_audio_right = 0;

        size_t m_num_underruns = 0;
        size_t m_num_overruns = 0;

        int m_samplerate = 0;
        int m_channels = 0;
        std::vector<uint8_t> m_audio;

        bool m_destination_available = true;
};

