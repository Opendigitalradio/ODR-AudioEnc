/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
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

#include "Outputs.h"
#include <string>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <cassert>

namespace Output {

using namespace std;

void Base::update_audio_levels(int16_t audiolevel_left, int16_t audiolevel_right)
{
    m_audio_left = audiolevel_left;
    m_audio_right = audiolevel_right;
}

File::File(const char *filename)
{
    m_fd = fopen(filename, "wb");
    if (m_fd == nullptr) {
        throw runtime_error(string("Error opening output file: ") + strerror(errno));
    }
}

File::File(FILE *fd) : m_fd(fd) { }

File::~File() {
    if (m_fd) {
        fclose(m_fd);
        m_fd = nullptr;
    }
}

bool File::write_frame(const uint8_t *buf, size_t len)
{
    if (m_fd == nullptr) {
        throw logic_error("Invalid usage of closed File output");
    }

    return fwrite(buf, len, 1, m_fd) == 1;
}

ZMQ::ZMQ() :
    m_ctx(),
    m_sock(m_ctx, ZMQ_PUB)
{
    // Do not wait at teardown to send all data out
    int linger = 0;
    m_sock.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
}

ZMQ::~ZMQ() {}

void ZMQ::connect(const char *uri, const char *keyfile)
{
    if (keyfile) {
        fprintf(stderr, "Enabling encryption\n");

        int rc = readkey(keyfile, m_secretkey);
        if (rc) {
            throw runtime_error("Error reading secret key");
        }

        const int yes = 1;
        m_sock.setsockopt(ZMQ_CURVE_SERVER,
                &yes, sizeof(yes));

        m_sock.setsockopt(ZMQ_CURVE_SECRETKEY,
                m_secretkey, CURVE_KEYLEN);
    }
    m_sock.connect(uri);
}

void ZMQ::set_encoder_type(encoder_selection_t& enc, int bitrate)
{
    m_encoder = enc;
    m_bitrate = bitrate;
}

bool ZMQ::write_frame(const uint8_t *buf, size_t len)
{
    switch (m_encoder) {
        case encoder_selection_t::fdk_dabplus:
            return send_frame(buf, len);
        case encoder_selection_t::toolame_dab:
            return write_toolame(buf, len);
    }
    throw logic_error("Unhandled encoder in ZMQ::write_frame");
}

bool ZMQ::write_toolame(const uint8_t *buf, size_t len)
{
    m_toolame_buffer.insert(m_toolame_buffer.end(),
            buf, buf + len);

    // ODR-DabMux expects frames of length 3*bitrate
    const auto frame_len = 3 * m_bitrate;
    while (m_toolame_buffer.size() > frame_len) {
        vec_u8 frame(frame_len);
        // this is probably not very efficient
        std::copy(m_toolame_buffer.begin(), m_toolame_buffer.begin() + frame_len, frame.begin());

        bool success = send_frame(frame.data(), frame.size());
        if (not success) {
            return false;
        }

        m_toolame_buffer.erase(m_toolame_buffer.begin(), m_toolame_buffer.begin() + frame_len);
    }
    return true;
}

bool ZMQ::send_frame(const uint8_t *buf, size_t len)
{
    if (m_framebuf.size() != ZMQ_HEADER_SIZE + len) {
        m_framebuf.resize(ZMQ_HEADER_SIZE + len);
    }

    zmq_frame_header_t *zmq_frame_header = (zmq_frame_header_t*)m_framebuf.data();

    try {
        switch (m_encoder) {
            case encoder_selection_t::fdk_dabplus:
                zmq_frame_header->encoder = ZMQ_ENCODER_FDK;
                break;
            case encoder_selection_t::toolame_dab:
                zmq_frame_header->encoder = ZMQ_ENCODER_TOOLAME;
                break;
        }

        zmq_frame_header->version = 1;
        zmq_frame_header->datasize = len;
        zmq_frame_header->audiolevel_left = m_audio_left;
        zmq_frame_header->audiolevel_right = m_audio_right;

        assert(ZMQ_FRAME_SIZE(zmq_frame_header) <= m_framebuf.size());

        memcpy(ZMQ_FRAME_DATA(zmq_frame_header), buf, len);

        m_sock.send(m_framebuf.data(), ZMQ_FRAME_SIZE(zmq_frame_header),
                ZMQ_DONTWAIT);
    }
    catch (zmq::error_t& e) {
        fprintf(stderr, "ZeroMQ send error !\n");
        return false;
    }

    return true;
}

}
