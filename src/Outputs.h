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

#pragma once
#include <vector>
#include <chrono>
#include <deque>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "common.h"
#include "zmq.hpp"
#include "ClockTAI.h"
#include "edi/TagItems.h"
#include "edi/TagPacket.h"
#include "edi/AFPacket.h"
#include "edi/Transport.h"
extern "C" {
#include "encryption.h"
}

namespace Output {

/*! \file Outputs.h
 *
 * Declaration of all outputs
 */

class Base {
    public:
        virtual ~Base() {};

        /*! Write a buffer of encoded data to the output */
        virtual bool write_frame(const uint8_t *buf, size_t len) = 0;

        /*! Update peak audio level information */
        virtual void update_audio_levels(
                int16_t audiolevel_left, int16_t audiolevel_right);

    protected:
        int16_t m_audio_left = 0;
        int16_t m_audio_right = 0;
};

class File : public Base {
    public:
        File(const char *filename);
        File(FILE *file);
        File(const File&) = delete;
        File& operator=(const File&) = delete;
        virtual ~File() override;

        virtual bool write_frame(const uint8_t *buf, size_t len) override;

    private:
        FILE *m_fd = nullptr;
};

/*! This defines the on-wire representation of a ZMQ message header.
 * It must be compatible with the definition in ODR-DabMux.
 *
 * The data follows right after this header */
struct zmq_frame_header_t
{
    uint16_t version; // we support version=1 now
    uint16_t encoder; // see ZMQ_ENCODER_XYZ

    /* length of the 'data' field */
    uint32_t datasize;

    /* Audio level, peak, linear PCM */
    int16_t audiolevel_left;
    int16_t audiolevel_right;

    /* Data follows this header */
} __attribute__ ((packed));

#define ZMQ_ENCODER_FDK 1
#define ZMQ_ENCODER_TOOLAME 2

#define ZMQ_HEADER_SIZE sizeof(struct zmq_frame_header_t)

/* The expected frame size incl data of the given frame */
#define ZMQ_FRAME_SIZE(f) (sizeof(struct zmq_frame_header_t) + f->datasize)

#define ZMQ_FRAME_DATA(f) ( ((uint8_t*)f)+sizeof(struct zmq_frame_header_t) )


class ZMQ: public Base {
    public:
        ZMQ();
        ZMQ(const ZMQ&) = delete;
        ZMQ& operator=(const ZMQ&) = delete;
        virtual ~ZMQ() override;

        void connect(const char *uri, const char *keyfile);
        void set_encoder_type(encoder_selection_t& enc, int bitrate);

        virtual bool write_frame(const uint8_t *buf, size_t len) override;

    private:
        zmq::context_t m_ctx;
        zmq::socket_t m_sock;

        int m_bitrate = 0;
        char m_secretkey[CURVE_KEYLEN+1];
        encoder_selection_t m_encoder = encoder_selection_t::fdk_dabplus;
        using vec_u8 = std::vector<uint8_t>;
        vec_u8 m_framebuf;
};


class EDI: public Base {
    public:
        EDI();
        EDI(const EDI&) = delete;
        EDI& operator=(const EDI&) = delete;
        virtual ~EDI() override;

        void add_udp_destination(const std::string& host, unsigned int port);
        void add_tcp_destination(const std::string& host, unsigned int port);

        void set_tist(bool enable, uint32_t delay_ms);

        bool enabled() const;

        virtual bool write_frame(const uint8_t *buf, size_t len) override;

    private:
        edi::configuration_t m_edi_conf;
        std::shared_ptr<edi::Sender> m_edi_sender;

        uint32_t m_timestamp = 0;
        uint32_t m_num_seconds_sent = 0;
        std::time_t m_edi_time = 0;
        std::time_t m_send_version_at_time = 0;

        edi::TagDSTI m_edi_tagDSTI;

        ClockTAI m_clock_tai;
        bool m_tist = false;
        uint32_t m_delay_ms = 0;
};

}
