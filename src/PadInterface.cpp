/* ------------------------------------------------------------------
 * Copyright (C) 2020 Matthias P. Braendli
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
#include "PadInterface.h"
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#define MESSAGE_REQUEST 1
#define MESSAGE_PAD_DATA 2

using namespace std;

void PadInterface::open(const std::string& pad_ident)
{
    m_pad_ident = pad_ident;

    m_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_sock == -1) {
        throw runtime_error("PAD socket creation failed: " + string(strerror(errno)));
    }

    int flags = fcntl(m_sock, F_GETFL);
    if (flags == -1) {
        std::string errstr(strerror(errno));
        throw std::runtime_error("TCP: Could not get socket flags: " + errstr);
    }

    if (fcntl(m_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::string errstr(strerror(errno));
        throw std::runtime_error("TCP: Could not set O_NONBLOCK: " + errstr);
    }

    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "/tmp/%s.audioenc", m_pad_ident.c_str());
    if (unlink(claddr.sun_path) == -1 and errno != ENOENT) {
        fprintf(stderr, "Unlinking of socket %s failed: %s\n", claddr.sun_path, strerror(errno));
    }

    int ret = bind(m_sock, (const struct sockaddr *) &claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        throw runtime_error("PAD socket bind failed " + string(strerror(errno)));
    }
}

vector<uint8_t> PadInterface::request(uint8_t padlen)
{
    if (m_pad_ident.empty()) {
        throw logic_error("Uninitialised PadInterface::request() called");
    }

    // Sending requests allows the PadEnc to know both the padlen, but also
    // will allow proper timing.

    uint8_t packet[2];

    packet[0] = MESSAGE_REQUEST; // Message type, to allow future expansion
    packet[1] = padlen;

    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "/tmp/%s.padenc", m_pad_ident.c_str());

    ssize_t ret = sendto(m_sock, packet, sizeof(packet), 0, (struct sockaddr*)&claddr, sizeof(struct sockaddr_un));
    if (ret == -1) {
        // This suppresses the -Wlogical-op warning
        if (errno == EAGAIN
#if EAGAIN != EWOULDBLOCK
                or errno == EWOULDBLOCK
#endif
                or errno == ECONNREFUSED
                or errno == ENOENT) {
            if (m_padenc_reachable) {
                fprintf(stderr, "ODR-PadEnc at %s not reachable\n", claddr.sun_path);
                m_padenc_reachable = false;
            }
        }
        else {
            fprintf(stderr, "PAD request send failed: %s\n", strerror(errno));
        }
    }
    else if (ret != sizeof(packet)) {
        fprintf(stderr, "PAD request incorrect length sent: %zu bytes of %zu transmitted\n",
                ret, sizeof(packet));
    }
    else if (not m_padenc_reachable) {
        fprintf(stderr, "ODR-PadEnc is now reachable at %s\n", claddr.sun_path);
        m_padenc_reachable = true;
    }

    vector<uint8_t> buffer(2048);

    while (true) {
        ret = recvfrom(m_sock, buffer.data(), buffer.size(), 0, nullptr, nullptr);

        if (ret == -1) {
            // This suppresses the -Wlogical-op warning
#if EAGAIN == EWOULDBLOCK
            if (errno != EAGAIN)
#else
            if (not (errno == EAGAIN or errno == EWOULDBLOCK))
#endif
            {
                throw runtime_error(string("Can't receive data: ") + strerror(errno));
            }

            return {};
        }
        else if (ret > 0) {
            buffer.resize(ret);

            // We could check where the data comes from, but since we're using UNIX sockets
            // the source is anyway local to the machine.

            if (buffer[0] == MESSAGE_PAD_DATA) {
                vector<uint8_t> pad_data(buffer.size() - 1);
                copy(buffer.begin() + 1, buffer.end(), pad_data.begin());
                return pad_data;
            }
            else {
                continue;
            }
        }
    }
}
