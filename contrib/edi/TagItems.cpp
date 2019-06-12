/*
   EDI output.
    This defines a few TAG items as defined ETSI TS 102 821 and
    ETSI TS 102 693

   Copyright (C) 2019
   Matthias P. Braendli, matthias.braendli@mpb.li

    http://www.opendigitalradio.org

   */
/*
   This file is part of ODR-DabMux.

   ODR-DabMux is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   ODR-DabMux is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with ODR-DabMux.  If not, see <http://www.gnu.org/licenses/>.
   */

#include "config.h"
#include "edi/TagItems.h"
#include <vector>
#include <iostream>
#include <string>
#include <stdint.h>
#include <stdexcept>

namespace edi {

std::vector<uint8_t> TagStarPTR::Assemble()
{
    //std::cerr << "TagItem *ptr" << std::endl;
    std::string pack_data("*ptr");
    std::vector<uint8_t> packet(pack_data.begin(), pack_data.end());

    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0x40);

    if (protocol.size() != 4) {
        throw std::runtime_error("TagStarPTR protocol invalid length");
    }
    packet.insert(packet.end(), protocol.begin(), protocol.end());

    // Major
    packet.push_back(0);
    packet.push_back(0);

    // Minor
    packet.push_back(0);
    packet.push_back(0);
    return packet;
}

std::vector<uint8_t> TagDSTI::Assemble()
{
    std::string pack_data("dsti");
    std::vector<uint8_t> packet(pack_data.begin(), pack_data.end());
    packet.reserve(256);

    // Placeholder for length
    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0);

    uint8_t dfctl = dflc % 250;
    uint8_t dfcth = dflc / 250;


    uint16_t dstiHeader = dfctl | (dfcth << 8) | (rfadf << 13) | (atstf << 14) | (stihf << 15);
    packet.push_back(dstiHeader >> 8);
    packet.push_back(dstiHeader & 0xFF);

    if (stihf) {
        packet.push_back(stat);
        packet.push_back((spid >> 8) & 0xFF);
        packet.push_back(spid & 0xFF);
    }

    if (atstf) {
        packet.push_back(utco);

        packet.push_back((seconds >> 24) & 0xFF);
        packet.push_back((seconds >> 16) & 0xFF);
        packet.push_back((seconds >> 8) & 0xFF);
        packet.push_back(seconds & 0xFF);

        packet.push_back((tsta >> 16) & 0xFF);
        packet.push_back((tsta >> 8) & 0xFF);
        packet.push_back(tsta & 0xFF);
    }

    if (rfadf) {
        for (size_t i = 0; i < rfad.size(); i++) {
            packet.push_back(rfad[i]);
        }
    }
    // calculate and update size
    // remove TAG name and TAG length fields and convert to bits
    uint32_t taglength = (packet.size() - 8) * 8;

    // write length into packet
    packet[4] = (taglength >> 24) & 0xFF;
    packet[5] = (taglength >> 16) & 0xFF;
    packet[6] = (taglength >> 8) & 0xFF;
    packet[7] = taglength & 0xFF;

    dflc = (dflc+1) % 5000;

    /*
    std::cerr << "TagItem dsti, packet.size " << packet.size() << std::endl;
    std::cerr << "              length " << taglength / 8 << std::endl;
    */
    return packet;
}

void TagDSTI::set_edi_time(const std::time_t t, int tai_utc_offset)
{
    utco = tai_utc_offset - 32;

    const std::time_t posix_timestamp_1_jan_2000 = 946684800;

    seconds = t - posix_timestamp_1_jan_2000 + utco;
}


std::vector<uint8_t> TagSSm::Assemble()
{
    std::string pack_data("ss");
    std::vector<uint8_t> packet(pack_data.begin(), pack_data.end());
    packet.reserve(istd_length + 16);

    packet.push_back((id >> 8) & 0xFF);
    packet.push_back(id & 0xFF);

    // Placeholder for length
    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0);
    packet.push_back(0);

    if (rfa > 0x1F) {
        throw std::runtime_error("TagSSm: invalid RFA value");
    }

    if (tid > 0x7) {
        throw std::runtime_error("TagSSm: invalid tid value");
    }

    if (tidext > 0x7) {
        throw std::runtime_error("TagSSm: invalid tidext value");
    }

    if (stid > 0x0FFF) {
        throw std::runtime_error("TagSSm: invalid stid value");
    }

    uint32_t istc = (rfa << 19) | (tid << 16) | (tidext << 13) | ((crcstf ? 1 : 0) << 12) | stid;
    packet.push_back((istc >> 16) & 0xFF);
    packet.push_back((istc >> 8) & 0xFF);
    packet.push_back(istc & 0xFF);

    for (size_t i = 0; i < istd_length; i++) {
        packet.push_back(istd_data[i]);
    }

    // calculate and update size
    // remove TAG name and TAG length fields and convert to bits
    uint32_t taglength = (packet.size() - 8) * 8;

    // write length into packet
    packet[4] = (taglength >> 24) & 0xFF;
    packet[5] = (taglength >> 16) & 0xFF;
    packet[6] = (taglength >> 8) & 0xFF;
    packet[7] = taglength & 0xFF;

    /*
    std::cerr << "TagItem SSm, length " << packet.size() << std::endl;
    std::cerr << "             istd_length " << istd_length << std::endl;
    */
    return packet;
}

std::vector<uint8_t> TagStarDMY::Assemble()
{
    std::string pack_data("*dmy");
    std::vector<uint8_t> packet(pack_data.begin(), pack_data.end());

    packet.resize(4 + 4 + length_);

    const uint32_t length_bits = length_ * 8;

    packet[4] = (length_bits >> 24) & 0xFF;
    packet[5] = (length_bits >> 16) & 0xFF;
    packet[6] = (length_bits >> 8) & 0xFF;
    packet[7] = length_bits & 0xFF;

    // The remaining bytes in the packet are "undefined data"

    return packet;
}

}

