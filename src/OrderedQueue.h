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

#ifndef _ORDERED_QUEUE_H_
#define _ORDERED_QUEUE_H_

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <map>
#include <vector>

class OrderedQueueData;

class OrderedQueue
{
    public:
        OrderedQueue(int32_t countModulo, size_t capacity);
        ~OrderedQueue();

        void push(int32_t count, const uint8_t* buf, size_t size);
        bool availableData();
        size_t pop(std::vector<uint8_t>& buf, int32_t* retCount=NULL);

    private:
        int32_t     _countModulo;
        size_t      _capacity;
        uint64_t    _duplicated;
        uint64_t    _overruns;        
        int32_t     _lastCount;

        std::map<int, OrderedQueueData*> _stock;
        typedef std::map<int, OrderedQueueData*>::iterator StockIterator;
};

class OrderedQueueData
{
    public:
        OrderedQueueData(const uint8_t* data, size_t size);
        ~OrderedQueueData();
        
        uint8_t* getData()  { return _data; }
        size_t   getSize()  { return _size; }

    private:
        uint8_t* _data;
        size_t _size;
};

#endif // _ORDERED_QUEUE_H_
