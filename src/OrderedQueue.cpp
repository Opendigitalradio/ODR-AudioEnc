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

#include "OrderedQueue.h"
#include <cstring>
#include <cstdio>
#include <stdint.h>

#define DEBUG(fmt, A...)   fprintf(stderr, "OrderedQueue: " fmt, ##A)
//#define DEBUG(x...)
#define ERROR(fmt, A...)   fprintf(stderr, "OrderedQueue: ERROR " fmt, ##A)

/*
 * 
 */
OrderedQueue::OrderedQueue(int countModulo, size_t capacity)
    :   _countModulo(countModulo),
        _capacity(capacity),
        _duplicated(0),
        _overruns(0),
        _lastCount(-1)
{
}

/*
 * 
 */
OrderedQueue::~OrderedQueue()
{
    StockIterator it = _stock.begin();
    while (it != _stock.end()) {
        delete it->second;
        it++;
    }
}

/*
 * 
 */
void OrderedQueue::push(int32_t count, const uint8_t* buf, size_t size)
{
//    DEBUG("OrderedQueue::push count=%d\n", count);
    count = (count+_countModulo) % _countModulo;
    
    // First frame makes the count initialisation.
    if( _lastCount == -1 )
    {
        _lastCount = (count+_countModulo-1)%_countModulo;
    }

    if (_stock.size() < _capacity) {
        StockIterator it = _stock.find(count);
        OrderedQueueData* oqd = new OrderedQueueData(buf, size);
        if (it == _stock.end()) {
            if (_stock.insert(std::make_pair(count, oqd)).second == false) {
                ERROR("%d not inserted\n", count);
                delete oqd;
            }
        }
        else {
            // count already exists, duplicated frame
            // Replace the old one by the new one.
            // the old one could a an old frame from the previous count loop
            delete it->second;
            it->second = oqd;
            _duplicated++;
            DEBUG("Duplicated count=%d\n", count);
        }
    }
    else {
        _overruns++;
        if (_overruns < 100)
            DEBUG("Overruns (size=%zu) count=%d not inserted\n", _stock.size(), count);
        else if (_overruns == 100)
            DEBUG("stop displaying Overruns\n");           
    }
}

/*
 * 
 */
bool OrderedQueue::availableData()
{
    // TODO Wait for filling gaps
    return _stock.size() > 0;
}

/*
 * 
 */
size_t OrderedQueue::pop(std::vector<uint8_t>& buf, int32_t* retCount)
{    
    size_t nbBytes = 0;
    uint32_t gap = 0;
    
    if (_stock.size() > 0) {
        int32_t nextCount = (_lastCount+1)%_countModulo;  
        bool found = false;
        do {
            StockIterator it = _stock.find(nextCount);
            if (it != _stock.end()) {
                OrderedQueueData* oqd = it->second;
                buf.resize(oqd->getSize());
                memcpy(buf.data(), oqd->getData(), oqd->getSize());
                nbBytes = oqd->getSize();
                delete oqd;
                _stock.erase(it);
                _lastCount = nextCount;
                if (retCount) *retCount = _lastCount;
                found = true;
            } else
            {
                if( _stock.size() < _capacity ) found = true;
                else {
                    // Search for the new reference count, starting from the current one                    
                    // This could be optimised, but the modulo makes things
                    // not easy.
                    gap++;
                    nextCount = (nextCount+1)%_countModulo;
                }
            }
        } while( !found );
    }

    if( gap > 0 )
    {
        DEBUG("Count jump of %d\n", gap);
    }
//    if (nbBytes > 0 && retCount) DEBUG("OrderedQueue::pop count=%d\n", *retCount);
    return nbBytes;
}

OrderedQueueData::OrderedQueueData(const uint8_t* data, size_t size)
{
    _data = new uint8_t[size];
    memcpy(_data, data, size);
    _size = size;
}

OrderedQueueData::~OrderedQueueData()
{
    delete [] _data;
}
