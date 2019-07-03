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

EDI::EDI() { }

EDI::~EDI() { }

void EDI::add_udp_destination(const std::string& host, int port)
{
    auto dest = make_shared<edi::udp_destination_t>();
    dest->dest_addr = host;
    m_edi_conf.dest_port = port;
    m_edi_conf.destinations.push_back(dest);

    // We cannot carry AF packets over UDP, because they would be too large.
    m_edi_conf.enable_pft = true;

    // TODO make FEC configurable
}

void EDI::add_tcp_destination(const std::string& host, int port)
{
    auto dest = make_shared<edi::tcp_client_t>();
    dest->dest_addr = host;
    if (dest->dest_port != 0 and dest->dest_port != port) {
        throw runtime_error("All EDI UDP outputs must be to the same destination port");
    }
    dest->dest_port = port;
    m_edi_conf.destinations.push_back(dest);

    m_edi_conf.dump = true;
}

bool EDI::enabled() const
{
    return not m_edi_conf.destinations.empty();
}

bool EDI::write_frame(const uint8_t *buf, size_t len)
{
    if (not m_edi_sender) {
        m_edi_sender = make_shared<edi::Sender>(m_edi_conf);
    }

    edi::TagStarPTR edi_tagStarPtr;
    edi_tagStarPtr.protocol = "DSTI";

    m_edi_tagDSTI.stihf = false;
    m_edi_tagDSTI.atstf = false;
    m_edi_tagDSTI.rfadf = false;
    // DFCT is handled inside the TagDSTI

    edi::TagSSm edi_tagPayload;
    // TODO make edi_tagPayload.stid configurable
    edi_tagPayload.istd_data = buf;
    edi_tagPayload.istd_length = len;

    // The above Tag Items will be assembled into a TAG Packet
    edi::TagPacket edi_tagpacket(m_edi_conf.tagpacket_alignment);

    // put tags *ptr, DETI and all subchannels into one TagPacket
    edi_tagpacket.tag_items.push_back(&edi_tagStarPtr);
    edi_tagpacket.tag_items.push_back(&m_edi_tagDSTI);
    edi_tagpacket.tag_items.push_back(&edi_tagPayload);

    m_edi_sender->write(edi_tagpacket);

    // TODO Handle TCP disconnect
    return true;
}

}
