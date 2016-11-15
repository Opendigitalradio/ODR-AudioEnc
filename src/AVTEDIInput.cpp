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

#include "AVTEDIInput.h"
#include <cstring>
#include <cstdio>
#include <stdint.h>
#include <limits.h>

#include "crc.h"
#include "OrderedQueue.h"

extern "C" {
#include <fec.h>
}

#define SUBCH_QUEUE_SIZE    (50)   /* In 24ms frames. Intermediate buffer */

#define RS_DECODE           1 /* Set to 0 to disable rs decoding */
#define RS_TEST1            0 /* Remove one fragment on each PFT */
#define RS_TEST2            0 /* Remove regularily fragments */
#define RS_TEST2_NBDROP     3 /* For RS_TEST2, nb packet remove on each time */

#define PRINTF(fmt, A...)   fprintf(stderr, fmt, ##A)
//#define PRINTF(x ...)
#define INFO(fmt, A...)   fprintf(stderr, "AVT EDI: " fmt, ##A)
//#define DEBUG(fmt, A...)   fprintf(stderr, "AVT EDI: " fmt, ##A)
#define DEBUG(X...)
#define ERROR(fmt, A...)   fprintf(stderr, "AVT EDI: ERROR " fmt, ##A)

static int hideFirstPFTErrors = 30; /* Hide the errors that can occurs on           */
                                    /* the first PFT, as they are likely incomplete */

#define TAG_NAME_DETI		(('d'<<24)|('e'<<16)|('t'<<8)|('i'))
#define TAG_NAME_EST		(('e'<<24)|('s'<<16)|('t'<<8))

/* ------------------------------------------------------------------
 *
 */
static void _dump(const uint8_t* buf, int size)
{
    for( int i = 0 ; i < size ; i ++)
    {
        PRINTF("%02X ", buf[i]);
        if( (i+1) % 16 == 0 ) PRINTF("\n");
    }
    if( size % 16 != 0 ) PRINTF("\n");
}

/* ------------------------------------------------------------------
 *
 */
static uint32_t unpack2(const uint8_t* buf)
{
    return( buf[0] << 8 |
            buf[1]);
}

/* ------------------------------------------------------------------
 *
 */
static uint32_t unpack3(const uint8_t* buf)
{
    return( buf[0] << 16 |
            buf[1] << 8 |
            buf[2]);
}

/* ------------------------------------------------------------------
 *
 */
static uint32_t unpack4(const uint8_t* buf)
{
    return( buf[0] << 24 |
            buf[1] << 16 |
            buf[2] << 8 |
            buf[3]);
}

/* ------------------------------------------------------------------
 * bitpos 0 : left most bit.
 * 
 */
static uint32_t unpack1bit(uint8_t byte, int bitpos)
{
    return (byte & 1 << (7-bitpos)) > (7-bitpos);
}


/* ------------------------------------------------------------------
 * 
 */
static bool _checkCRC(uint8_t* buf, size_t length)
{
    if (length <= 2) return false;
    
    uint16_t CRC = unpack2(buf+length-2);

    uint16_t crc = 0xffff;
    crc = crc16(crc, buf, length-2);
    crc ^= 0xffff;          

    return (CRC == crc);
}

/* ------------------------------------------------------------------
 * 
 */
AVTEDIInput::AVTEDIInput(uint32_t fragmentTimeoutMs)
    : _fragmentTimeoutMs(fragmentTimeoutMs)
{
    _subChannelQueue = new OrderedQueue(5000, SUBCH_QUEUE_SIZE);
}

/* ------------------------------------------------------------------
 *
 */
AVTEDIInput::~AVTEDIInput()
{
    PFTIterator it = _pft.begin();
    while (it != _pft.end()) {
        delete it->second;
        it++;
    }
    delete _subChannelQueue;
}

/* ------------------------------------------------------------------
 *
 */
bool AVTEDIInput::pushData(uint8_t* buf, size_t length)
{
    bool identified = false;
    
    if (length >= 12 && buf[0] == 'P' && buf[1] == 'F')
    {

#if RS_TEST2
        static int count=0;
        if (++count%1421<RS_TEST2_NBDROP)
            identified = true;
        else
#endif // RS_TEST2           
        identified = _pushPFTFrag(buf, length);
            
    }
    else if (length >= 10 && buf[0] == 'A' && buf[1] == 'F')
    {
        identified = _pushAF(buf, length, false);            
    }
    return identified;
}

/* ------------------------------------------------------------------
 *
 */
size_t AVTEDIInput::popFrame(std::vector<uint8_t>& data, int32_t& frameNumber)
{  
    return _subChannelQueue->pop(data, &frameNumber);
}

/* ------------------------------------------------------------------
 *
 */
bool AVTEDIInput::_pushPFTFrag(uint8_t* buf, size_t length)
{
    PFTFrag* frag = new PFTFrag(buf, length);
    bool isValid = frag->isValid();
    if (!isValid) {
        delete frag;
    } else {
        // Find PFT
        PFT* pft = NULL;
        PFTIterator it = _pft.find(frag->Pseq());        
        if (it != _pft.end()) {
            pft = it->second;
        } else {
            // create PFT is new
            pft = new PFT(frag->Pseq(), frag->Fcount());
            if (_pft.insert(std::make_pair(frag->Pseq(), pft)).second == false)
            {
                // Not inserted
                delete pft;
                pft = NULL;
            }
            it = _pft.find(frag->Pseq());
        }        

        if (pft) {
            // Add frag to PFT
            pft->pushPFTFrag(frag);
            
            // If the PFT is complete, extract the AF
            if (pft->complete()) {
                std::vector<uint8_t> af;
                bool ok = pft->extractAF(af);
                
                if (ok) {
                    _pushAF(af.data(), af.size(), ok);
                } else {
                    ERROR("AF Frame Corrupted, Size=%zu\n", af.size());
                    //_dump(af.data(), 10);                                        
                }

                _pft.erase(it);
                delete pft;
            }
        }
    }

    // Check old incomplete PFT to either try to extract AF or discard it
    // TODO
    const auto now = std::chrono::steady_clock::now();
    const auto timeout_duration = std::chrono::milliseconds(_fragmentTimeoutMs);

    PFTIterator it = _pft.begin();
    while (it != _pft.end()) {
        PFT* pft = it->second;
        bool erased = false;
        if (pft) {
            const auto creation = pft->creation();
            const auto diff = now - creation;
            if (diff > timeout_duration) {                
                //DEBUG("PFT timeout\n");
                std::vector<uint8_t> af;
                bool ok = pft->extractAF(af);                
                if (ok) {
                    _pushAF(af.data(), af.size(), ok);
                } else {
                    //ERROR("AF Frame CorruptedSize=%zu\n", af.size());
                    //_dump(af.data(), 10);                                        
                }

                it = _pft.erase(it);
                delete pft;
                erased = true;
            }
        }
        if (!erased) ++it;
    }

    return isValid;
}

/* ------------------------------------------------------------------
 *
 */
bool AVTEDIInput::_pushAF(uint8_t* buf, size_t length, bool checked)
{
    bool ok = checked;

    // Check the AF integrity
    if (!ok) {
       // EDI specific, must have a CRC.
        if (length >= 12) {
            ok = (buf[0] == 'A' && buf[1] == 'F');
            ok &= _checkCRC(buf, length);
        }
    }

    int index = 0;
    
    index += 2;
    uint32_t LEN = unpack4(buf+index); index += 4;
    ok = (LEN == length-12);
    uint32_t SEQ = unpack2(buf+index); index += 2;

    if (ok) {
        uint32_t CF = unpack1bit(buf[index], 0);
        uint32_t MAJ = (buf[index]&0x70) >> 4;
        uint32_t MIN = (buf[index]&0x0F);
        index += 1;
        uint32_t PT = buf[index]; index += 1;
        
        // EDI specific
        ok = (CF == 1 && PT == 'T' && MAJ == 1 && MIN == 0);

//        DEBUG("AF Header: LEN=%u SEQ=%u CF=%u MAJ=%u MIN=%u PT=%c ok=%d\n",
//            LEN, SEQ, CF, MAJ, MIN, PT, ok);
    }

    if (ok) {
        // Extract the first stream and FrameCount from AF
        int tagIndex = index;
        uint32_t frameCount;
        bool frameCountFound = false;
        int est0Index = 0;
        size_t est0Length = 0;
        // Iterate through tags
        while (tagIndex < length - 2/*CRC*/ - 8/*Min tag length*/ && (!frameCountFound || est0Index==0) )
        {
            uint32_t tagName = unpack4(buf+tagIndex); tagIndex += 4;
            uint32_t tagLen = unpack4(buf+tagIndex); tagIndex += 4;
            uint32_t tagLenByte = (tagLen+7)/8;
//            DEBUG("TAG %c%c%c%c size %u bits %u bytes\n",
//                    tagName>>24&0xFF, tagName>>16&0xFF, tagName>>8&0xFF, tagName&0xFF,
//                    tagLen, tagLenByte);
            
            if (tagName == TAG_NAME_DETI) {
                uint32_t FCTH = buf[tagIndex] & 0x1F;
                uint32_t FCT = buf[tagIndex+1];
                frameCount = FCTH * 250 + FCT;
                frameCountFound = true;
//                DEBUG("frameCount=%u\n", frameCount);
            } else if ((tagName & 0xFFFFFF00) ==  TAG_NAME_EST) {                
                est0Index = tagIndex+3 /*3 bytes SSTC*/;
                est0Length = tagLenByte-3;
//                DEBUG("Stream found at index %u, size=%zu\n", est0Index, est0Length);
            }

            tagIndex += tagLenByte;
        }
        if (frameCountFound && est0Index !=0) {
            _subChannelQueue->push(frameCount, buf+est0Index, est0Length);
        } else {
            ok = false;
        }
    }

    return ok;
}

/* ------------------------------------------------------------------
 * ------------------------------------------------------------------
 * ------------------------------------------------------------------
 * ------------------------------------------------------------------
 */

/* ------------------------------------------------------------------
 *
 */
//static int nbPFTFrag = 0;
PFTFrag::PFTFrag(uint8_t* buf, size_t length)
{
    //DEBUG("+ PFTFrag %d\n", ++nbPFTFrag);
    _valid = _parse(buf, length);    
}

/* ------------------------------------------------------------------
 *
 */
PFTFrag::~PFTFrag()
{    
    //DEBUG("- PFTFrag %d\n", --nbPFTFrag);
}

/* ------------------------------------------------------------------
 *
 */
bool PFTFrag::_parse(uint8_t* buf, size_t length)
{
    int index = 0;
    
    // Parse PFT Fragment Header (ETSI TS 102 821 V1.4.1 ch7.1)
    index += 2; // Psync
    
    _Pseq = unpack2(buf+index); index += 2;
    _Findex = unpack3(buf+index); index += 3;
    _Fcount = unpack3(buf+index); index += 3;
    _FEC = unpack1bit(buf[index], 0);
    _Addr = unpack1bit(buf[index], 1);
    _Plen = unpack2(buf+index) & 0x3FFF; index += 2;
    
    // Optional RS Header
    _RSk = 0;
    _RSz = 0;
    if (_FEC) {
        _RSk = buf[index]; index += 1;
        _RSz = buf[index]; index += 1;
    }
    
    // Optional transport header
    _Source = 0;
    _Dest = 0;
    if (_Addr) {
        _Source = unpack2(buf+index); index += 2;
        _Dest = unpack2(buf+index); index += 2;
    }

    index += 2;
    bool isValid = (_FEC==0) || _checkCRC(buf, index);
    isValid &= length == index + _Plen;
 
    if (!isValid) {
//        DEBUG("PFT isValid=%d Pseq=%u Findex=%u Fcount=%u FEC=%u "
//            "Addr=%u Plen=%u",
//            isValid, _Pseq, _Findex, _Fcount, _FEC,
//            _Addr, _Plen);
        if (_FEC) PRINTF(" RSk=%u RSz=%u", _RSk, _RSz);
        if (_Addr) PRINTF(" Source=%u Dest=%u", _Source, _Dest);
        PRINTF("\n");
    }

    if (isValid) {
        _payload.resize(_Plen);
        memcpy(_payload.data(), buf+index, _Plen);
    }

    return isValid;
}

/* ------------------------------------------------------------------
 * ------------------------------------------------------------------
 * ------------------------------------------------------------------
 * ------------------------------------------------------------------
 */
void* PFT::_rs_handler = NULL;

/* ------------------------------------------------------------------
 *
 */
//static int nbPFT = 0;
PFT::PFT(uint32_t Pseq, uint32_t Fcount)
    : _frags(NULL)
    , _Pseq(Pseq)
    , _Fcount(Fcount)
    , _Plen(0)
    , _nbFrag(0)
    , _RSk(0)
    , _RSz(0)
    , _cmax(0)
    , _rxmin(0)
    , _creation(std::chrono::steady_clock::now())
{
//    DEBUG("+ PFT %d\n", ++nbPFT);
    if (Fcount > 0) {
        _frags = new PFTFrag* [Fcount];
        memset(_frags, 0, Fcount*sizeof(PFTFrag*));
    }
}

/* ------------------------------------------------------------------
 *
 */
PFT::~PFT()
{
//    DEBUG("- PFT %d\n", --nbPFT);
    if (_frags) {
        for (int i=0 ; i<_Fcount ; i++) {
            delete _frags[i];
        }
        delete [] _frags;
    }
}

/* ------------------------------------------------------------------
 * static
 */
void PFT::_initRSDecoder()
{
#if RS_DECODE
    if (!_rs_handler) {       
        // From ODR-DabMux: PFT.h/cpp and ReedSolomon.h/cpp

        // Create the RS(k+p,k) encoder
        const int firstRoot = 1; // Discovered by analysing EDI dump
        const int gfPoly = 0x11d;

        // The encoding has to be 255, 207 always, because the chunk has to
        // be padded at the end, and not at the beginning as libfec would
        // do
        const int N = 255;
        const int K = 207;
        const int primElem = 1;
        const int symsize = 8;
        const int nroots = N - K; // For EDI PFT, this must be 48
        const int pad = ((1 << symsize) - 1) - N; // is 255-N

        _rs_handler = init_rs_char(symsize, gfPoly, firstRoot, primElem, nroots, pad);


/* TEST RS CODE */
#if 0

        // Populate data
        uint8_t data[255];
        memset(data, 0x00, 255);
        for (int i=0;i<207;i++) data[i] = i%10;

        // Add RS Code
        encode_rs_char(_rs_handler, data, data+207);
        _dump(data, 255);
        
        // Disturb data
        for (int i=50; i<50+24; i++) data[i]+=0x50;
        
        // Correct data
        int nbErr =  decode_rs_char(_rs_handler, data, NULL, 0);
        printf("nbErr=%d\n", nbErr);
        _dump(data, 255);

        // Check data
        for (int i=0;i<207;i++) {
            if (data[i] != i%10) {
                printf("Error position %d %hhu != %d\n", i, data[i], i%10);
            }
        }

        // STOP (sorry :-| )
        int* i=0;
        *i = 9;
#endif // 0       
    }
#endif
}

/* ------------------------------------------------------------------
 *
 */
void PFT::pushPFTFrag(PFTFrag* frag)
{
    uint32_t Findex = frag->Findex();
#if RS_TEST1    
    if (Findex != 0 && _frags[Findex] == NULL)  /* TEST */
#else
    if (_frags[Findex] == NULL)
#endif
    {
        _frags[Findex] = frag;
        _nbFrag++;

        // Calculate the minimum number of fragment necessary to apply FEC
        // This can't be done with the last fragment that does may have a smaller size
        // ETSI TS 102 821 V1.4.1 ch 7.4.4       
        if (_Plen == 0 && (Findex == 0 || Findex < (_Fcount-1)))
        {
            _Plen = frag->Plen();
        }

        if (_cmax == 0 && frag->FEC() && (Findex == 0 || Findex < (_Fcount-1)) && _Plen>0)
        {
            _RSk = frag->RSk();
            _RSz = frag->RSz();
            _cmax = (_Fcount*_Plen) / (_RSk+48);
            _rxmin = _Fcount - (_cmax*48)/_Plen;
        }
    } else {
        // Already received, delete the fragment
        delete frag;
    }
}

/* ------------------------------------------------------------------
 *
 */
bool PFT::complete()
{
#if RS_TEST1    
    return _nbFrag == _Fcount-1;
#else
    return _nbFrag == _Fcount;
#endif
}

/* ------------------------------------------------------------------
 *
 */
bool PFT::_canAttemptToDecode()
{
    if (complete()) return true;
    
    if (_cmax>0 && _nbFrag >= _rxmin) return true;    

    return false;
}

/* ------------------------------------------------------------------
 *
 */
bool PFT::extractAF(std::vector<uint8_t>& afdata)
{
    bool ok = false;
//    DEBUG("extractAF from PFT %u. Fcount=%u nbFrag=%u Plen=%u cmax=%u rxmin=%u RSk=%u RSz=%u\n",
//            _Pseq, _Fcount, _nbFrag, _Plen, _cmax, _rxmin, _RSk, _RSz);

    if (_canAttemptToDecode()) {
        int totCorrectedErr = 0;

        if (_cmax > 0)      // FEC present.
        {
            int j, k;
            uint8_t* p_data_w;
            uint8_t* p_data_r;
            size_t data_len = 0;
            
            // Re-assemble RS block
            uint8_t rs_block[_Plen*_Fcount];
            int eras_pos[_cmax][/*48*/255]; /* 48 theoritically but ... */
            int no_eras[_cmax];
            memset(no_eras, 0, sizeof(no_eras));

            p_data_w = rs_block;
            for (j = 0; j < _Fcount; ++j) {
                if (!_frags[j]) // fill with zeros if fragment is missing
                {
                    for (int k = 0; k < _Plen; k++) {
                        int pos = k * _Fcount;
                        p_data_w[pos] = 0x00;
                        int chunk = pos / (_RSk+48);
                        int chunkpos = (pos) % (_RSk+48);
                        if (chunkpos > _RSk) {
                            chunkpos += (207-_RSk);
                        }
                        eras_pos[chunk][no_eras[chunk]] = chunkpos;
                        no_eras[chunk]++;
                    }
                } else {
                    uint8_t* p_data_r = _frags[j]->payload();
                    for (k = 0; k < _frags[j]->Plen(); k++)
                        p_data_w[k * _Fcount] = *p_data_r++;
                    for (k = _frags[j]->Plen(); k < _Plen; k++)
                        p_data_w[k * _Fcount] = 0x00;
                }
                p_data_w++;
            }

            // Apply RS Code
#if RS_DECODE
            uint8_t rs_chunks[255 * _cmax];
            _initRSDecoder();
            if (_rs_handler) {
                k = _RSk;
                memset(rs_chunks, 0, sizeof(rs_chunks));
                p_data_w = rs_chunks;
                p_data_r = rs_block;
                for (j = 0; j < _cmax; j++) {
                    memcpy(p_data_w, p_data_r, k);
                    p_data_w += k;
                    p_data_r += k;
                    if (k < 207)
                        memset(p_data_w, 0, 207 - k);
                    p_data_w += 207 - k; 
                    memcpy(p_data_w, p_data_r, 48);
                    p_data_w += 48;
                    p_data_r += 48;
                }

                p_data_r = rs_chunks;
                for (j = 0 ; j < _cmax && totCorrectedErr != -1 ; j++) {
#if RS_TEST1 || RS_TEST2
                    if (no_eras[j]>0) {
                        DEBUG("RS Chuck %d: %d errors\n", j, no_eras[j]);
                    }                        
#endif                                           
                    int nbErr = decode_rs_char(_rs_handler, p_data_r, eras_pos[j], no_eras[j]);
//                    int nbErr = decode_rs_char(_rs_handler, p_data_r, NULL, 0);
                    if (nbErr >= 0) {
#if RS_TEST1 || RS_TEST2
                        if (nbErr > 0) DEBUG("RS Chuck %d: %d corrections\n", j, nbErr);
#endif                       
                        totCorrectedErr += nbErr;
                    } else {
#if RS_TEST1 || RS_TEST2
                        DEBUG("RS Chuck %d: too many errors\n", j);
#endif
                        totCorrectedErr = -1;
                    }
                    p_data_r += 255;
                }
#if RS_TEST1 || RS_TEST2
                if (totCorrectedErr>0) {
                    DEBUG("RS corrected %d errors in %d chunks\n", totCorrectedErr, _cmax);
                }
#endif
            }
#endif // RS_DECODE
            // Assemble AF frame from rs code
            /* --- re-assemble packet from Reed-Solomon block ----------- */
            afdata.resize(_Plen*_Fcount);
            p_data_w = afdata.data();
#if RS_DECODE           
            p_data_r = rs_chunks;
            for (j = 0; j < _cmax; j++) {
                memcpy(p_data_w, p_data_r, _RSk);
                p_data_w += _RSk;
                p_data_r += 255;
                data_len += _RSk;
            }
#else
            p_data_r = rs_block;
            for (j = 0; j < _cmax; j++) {
                memcpy(p_data_w, p_data_r, _RSk);
                p_data_w += _RSk;
                p_data_r += _RSk + 48;
                data_len += _RSk;
            }
#endif // RS_DECODE
            data_len -= _RSz;
            afdata.resize(data_len);
        } else {            // No Fec Just assemble packets
            afdata.resize(0);
            for (int j = 0; j < _Fcount; ++j) {
                if (_frags[j])
                {
                    afdata.insert(afdata.end(),
                       _frags[j]->payloadVector().begin(), _frags[j]->payloadVector().end());
                }
            }
        }

        // EDI specific, must have a CRC.
        if( afdata.size()>=12 ) {
            ok = _checkCRC(afdata.data(), afdata.size());
            if (ok && totCorrectedErr > 0) {
                if (hideFirstPFTErrors==0) {
                    INFO("AF reconstructed from %u/%u PFT fragments\n", _nbFrag, _Fcount);
                }
            }
            if (!ok && totCorrectedErr == -1) {
                if (hideFirstPFTErrors==0) {
                    ERROR("Too many errors to reconstruct AF from %u/%u PFT fragments\n", _nbFrag, _Fcount);
                }
            }
        }
    }
    else {
       if (hideFirstPFTErrors==0) {
           ERROR("Not enough fragments to reconstruct AF from %u/%u PFT fragments (min=%u)\n", _nbFrag, _Fcount, _rxmin);
       }
    }
    
    if( hideFirstPFTErrors > 0 ) hideFirstPFTErrors--;

    return ok;
}
