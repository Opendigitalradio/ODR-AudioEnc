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

#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>

/*! \file PadInterface.h
 *
 * Handles communication with ODR-PadEnc using a socket
 */

class PadInterface {
    public:
        /*! Create a new PAD data interface that binds to /tmp/pad_ident.audioenc and
         * communicates with ODR-PadEnc at /tmp/pad_ident.padenc
         */
        void open(const std::string &pad_ident);

        std::vector<uint8_t> request(uint8_t padlen);

    private:
        std::string m_pad_ident;
        int m_sock = -1;
        bool m_padenc_reachable = true;
};
