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

#include "config.h"
#include "StatsPublish.h"
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <iterator>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std;

StatsPublisher::StatsPublisher(const string& socket_path) :
    m_socket_path(socket_path)
{
    // The client socket binds to a socket whose name depends on PID, and connects to
    // `socket_path`

    m_sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (m_sock == -1) {
        throw runtime_error("Stats socket creation failed: " + string(strerror(errno)));
    }

    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "/tmp/odr-audioenc.%ld", (long) getpid());

    int ret = bind(m_sock, (const struct sockaddr *) &claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        throw runtime_error("Stats socket bind failed " + string(strerror(errno)));
    }
}

StatsPublisher::~StatsPublisher()
{
    if (m_sock != -1) {
        close(m_sock);
    }
}

void StatsPublisher::update_audio_levels(int16_t audiolevel_left, int16_t audiolevel_right)
{
    m_audio_left = audiolevel_left;
    m_audio_right = audiolevel_right;
}

void StatsPublisher::update_decoded_audio(const std::vector<uint8_t>& audio, int samplerate, int channels)
{
    copy(audio.cbegin(), audio.cend(), back_inserter(m_audio));

    if ((m_samplerate != 0 and samplerate != m_samplerate) or
        (m_channels != 0 and channels != m_channels)) {
        m_audio.clear();
    }
    m_samplerate = samplerate;
    m_channels = channels;
}

void StatsPublisher::notify_underrun()
{
    m_num_underruns++;
}

void StatsPublisher::notify_overrun()
{
    m_num_overruns++;
}

void StatsPublisher::send_stats()
{
    // Manually build YAML, as it's quite easy.
    stringstream yaml;
    yaml << "---\n";
    yaml << "program: " << PACKAGE_NAME << "\n";
    yaml << "version: " <<
#if defined(GITVERSION)
            GITVERSION
#else
            PACKAGE_VERSION
#endif
            << "\n";
    yaml << "audiolevels: { left: " << m_audio_left << ", right: " << m_audio_right << "}\n";
    yaml << "driftcompensation: { underruns: " << m_num_underruns << ", overruns: " << m_num_overruns << "}\n";
    yaml << "samplerate: " << m_samplerate << "\n";
    yaml << "channels: " << m_channels << "\n";

    yaml << "...\n";
    const auto yamlstr = yaml.str();

    vector<uint8_t> packet(yamlstr.size());
    copy(yamlstr.cbegin(), yamlstr.cend(), packet.begin());
    copy(m_audio.begin(), m_audio.end(), back_inserter(packet));
    m_audio.clear();

    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "%s", m_socket_path.c_str());

    int ret = sendto(m_sock, packet.data(), packet.size(), 0,
            (struct sockaddr *) &claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        // This suppresses the -Wlogical-op warning
        if (errno == EAGAIN
#if EAGAIN != EWOULDBLOCK
                or errno == EWOULDBLOCK
#endif
                or errno == ECONNREFUSED
                or errno == ENOENT) {
            if (m_destination_available) {
                fprintf(stderr, "Stats destination not available at %s\n", m_socket_path.c_str());
                m_destination_available = false;
            }
        }
        else {
            fprintf(stderr, "Statistics send failed: %s\n", strerror(errno));
        }
    }
    else if (ret != (ssize_t)packet.size()) {
        fprintf(stderr, "Statistics send incorrect length: %d bytes of %zu transmitted\n",
                ret, packet.size());
    }
    else if (not m_destination_available) {
        fprintf(stderr, "Stats destination is now available at %s\n", m_socket_path.c_str());
        m_destination_available = true;
    }

    m_audio_left = 0;
    m_audio_right = 0;
}
