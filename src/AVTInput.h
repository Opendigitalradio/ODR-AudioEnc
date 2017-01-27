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
 * This input comunicate with AVT encoder
 *
 * The encoded frames and reassambled
 * The PAD Frames are sent to the encoder for insertion.
 * The encoder is remotely controled to set bitrate and audio mode.
 *
 */

#ifndef _AVT_INPUT_H_
#define _AVT_INPUT_H_

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <queue>
#include <vector>

class UdpSocket;
class UdpPacket;
class AVTEDIInput;
class OrderedQueue;

class AVTInput
{
    public:
        AVTInput(const std::string& input_uri, const std::string& output_uri, uint32_t pad_port,
                size_t jitterBufferSize = 40);
        ~AVTInput();

        /*! Open the file and prepare the wav decoder.
         *
         * \return nonzero on error
         */
        int prepare(void);
                
        /*! Inform class and remove encoder about the bitrate and audio mode
         * 
         * \return nonzero on error
         */
        int setDabPlusParameters(int bitrate, int channels, int sample_rate, bool sbr, bool ps);

        /*! Read incomming frames from the encoder, reorder and reassemble then into DAB+ superframes
         *! Give the next reassembled audio frame (120ms for DAB+) 
         * 
         * \return the size of the frame or 0 if none are available yet
         */
        ssize_t getNextFrame(std::vector<uint8_t> &buf);
        
        /*! Store a new PAD frame.
         *! Frames are sent to the encoder on request
         */
        void pushPADFrame(const uint8_t* buf, size_t size);
        
        /* \return true if PAD Frame queue is full */
        bool padQueueFull();
        

    private:
        std::string _input_uri;
        std::string _output_uri;
        uint32_t _pad_port;
        size_t _jitterBufferSize;
        
        UdpSocket*      _input_socket;
        UdpPacket*      _input_packet;
        UdpSocket*      _output_socket;
        UdpPacket*      _output_packet;
        UdpSocket*      _input_pad_socket;
        UdpPacket*      _input_pad_packet;
        AVTEDIInput*    _ediInput;
        OrderedQueue*   _ordered;
        std::queue< std::vector<uint8_t>* > _padFrameQueue;

        int32_t _subChannelIndex;
        int32_t _bitRate;
        int32_t _audioMode;
        int32_t _monoMode;
        int32_t _dac;
        size_t _dab24msFrameSize;
        uint32_t _dummyFrameNumber;
        bool _frameAlligned;
        uint8_t* _currentFrame;
        size_t _currentFrameSize;
        int32_t _nbFrames;
        uint8_t* _nextFrameIndex;

        bool _parseURI(const char* uri, std::string& address, long& port);
        int _openSocketSrv(UdpSocket* socket, const char* uri);
        int _openSocketCli(UdpSocket* socket, UdpPacket* packet, const char* uri);
        
        void _sendCtrlMessage(UdpSocket* socket, UdpPacket* packet);
        void _sendPADFrame(UdpPacket* packet = NULL);
        void _interpretMessage(const uint8_t* data, size_t size, UdpPacket* packet = NULL);
        bool _checkMessage();
        void _purgeMessages();

        /*! Read length bytes into buf.
         *
         * \return the number of bytes read.
         */
        ssize_t _read(uint8_t* buf, size_t length, bool onlyOnePacket=false);
        
        /*! Push new data to edi decoder
         * \return false is data is not EDI
         */
        bool _ediPushData(uint8_t* buf, size_t length);
        
        size_t _ediPopFrame(std::vector<uint8_t>& data, int32_t& frameNumber);
        
        /*! Test Bytes 1,2,3 for STI detection */
        bool _isSTI(const uint8_t* buf);
        
        /*! Find and extract the DAB frame from UDP/RTP/STI received frame
         * \param   frameNumber will contain the frameNumber
         * \param   dataSize will contain the actual DAB frame size
         * \return  Pointer to first byte of the DAB frame, or NULL if not found
         */
        const uint8_t* _findDABFrameFromUDP(const uint8_t* buf, size_t size,
                                    int32_t& frameNumber, size_t& dataSize);

        /*! Read and store one frame from encoder
         *
         * \return true if a data has been received
         */
        bool _readFrame();

        /*! Output info about received frames*/
        enum _frameType {
            _typeEDI,
            _typeSTI,
            _typeSTIRTP,
            _typeCantExtract
        };
        _frameType _lastInfoFrameType;
        size_t _lastInfoSize;
        size_t _infoNbFrame;
        void _info(_frameType type, size_t size);
};

#endif // _AVT_INPUT_H_
