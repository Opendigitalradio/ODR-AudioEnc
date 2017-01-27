/* ------------------------------------------------------------------
 * Copyright (C) 2017 AVT GmbH - Fabien Vercasson
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
 
 
/*! \section AVT Input
 *
 * Extract audio frame from EDI frames produced by AVT encoder.
 *
 * The EDI frames are not special, it is just assumed that the audio is transported
 * into the first stream.
 *
 * PFT with spreaded packets is supported.	TODO
 * Error correction is applied			TODO
 * AF without PFT supported			TODO
 * Resend not supported
 * 
 * ref: ETSI TS 102 821 V1.4.1
 *      ETSI TS 102 693 V1.1.2
 */

#ifndef _AVT_EDI_INPUT_
#define _AVT_EDI_INPUT_

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <map>
#include <vector>
#include <chrono>

class OrderedQueue;
class PFTFrag;
class PFT;
class EDISubCh;

/* ------------------------------------------------------------------
 *
 */
class AVTEDIInput
{
    public:
        /*\param fragmentTimeoutMs How long to wait for all fragment before applying FEC or dropping old frames*/
        AVTEDIInput(uint32_t fragmentTimeoutMs = 120);
        ~AVTEDIInput();

        /*! Push new data to edi decoder
         * \return false is data is not EDI
         */
        bool pushData(uint8_t* buf, size_t length);
        
        /*! Give next available audio frame from EDI
         * \return The size of the buffer. 0 if not data available
         */
        size_t popFrame(std::vector<uint8_t>& data, int32_t& frameNumber);

    private:
        uint32_t _fragmentTimeoutMs;
        std::map<int, PFT*>  _pft;
        typedef std::map<int, PFT*>::iterator PFTIterator;
        
        OrderedQueue*   _subChannelQueue;

        bool _pushPFTFrag(uint8_t* buf, size_t length);
        bool _pushAF(uint8_t* buf, size_t length, bool checked);
};

/* ------------------------------------------------------------------
 *
 */
class PFTFrag
{
    public:
        PFTFrag(uint8_t* buf, size_t length);
        ~PFTFrag();
        
        inline bool isValid() { return _valid; }
        inline uint32_t Pseq() { return _Pseq; }
        inline uint32_t Findex() { return _Findex; }
        inline uint32_t Fcount() { return _Fcount; }
        inline uint32_t FEC() { return _FEC; }
        inline uint32_t Plen() { return _Plen; }
        inline uint32_t RSk() { return _RSk; }
        inline uint32_t RSz() { return _RSz; }
        inline uint8_t* payload() { return _payload.data(); }
        inline const std::vector<uint8_t>& payloadVector()
            { return _payload; }

    private:
        std::vector<uint8_t> _payload;

        uint32_t _Pseq;
        uint32_t _Findex;
        uint32_t _Fcount;
        uint32_t _FEC;
        uint32_t _Addr;
        uint32_t _Plen;
        uint32_t _RSk;
        uint32_t _RSz;
        uint32_t _Source;
        uint32_t _Dest;
        bool _valid;
        
        bool _parse(uint8_t* buf, size_t length);       
};

/* ------------------------------------------------------------------
 *
 */
class PFT
{
    public:
        PFT(uint32_t Pseq, uint32_t Fcount);
        ~PFT();

        /*! the given frag belongs to the PFT class,
         *! it will be deleted by the class */
        void pushPFTFrag(PFTFrag* frag);

        /* \return true if all framgnements are received*/
        bool complete();
        
        /*! try to build the AF with received fragments.
         *! Apply error correction if necessary (missing packets/CRC errors)
         * \return true if the AF is completed
         */
        bool extractAF(std::vector<uint8_t>& afdata);  
        
        inline std::chrono::steady_clock::time_point creation()
            { return _creation; }

    private:
        PFTFrag** _frags;
        uint32_t _Pseq;
        uint32_t _Fcount;
        uint32_t _Plen;
        uint32_t _nbFrag;
        uint32_t _RSk;
        uint32_t _RSz;
        uint32_t _cmax;
        uint32_t _rxmin;

        std::chrono::steady_clock::time_point _creation;  
        
        bool _canAttemptToDecode();
        
        static void* _rs_handler;
        static void _initRSDecoder();
};

/* ------------------------------------------------------------------
 *
 */
class EDISubCh {
    public:
        EDISubCh(uint8_t* buf, size_t length);
        ~EDISubCh();

        inline uint32_t frameCount() { return _frameCount; }
        inline uint8_t* payload() { return _payload.data(); }
        inline const std::vector<uint8_t>& payloadVector()
            { return _payload; }

    private:
        uint32_t _frameCount;
        std::vector<uint8_t> _payload;
};

#endif // _AVT_EDI_INPUT_
