/*
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
 *
 * Matthias P. Braendli, matthias.braendli@mpb.li
 */

/*!
 * \file SampleQueue.h
 * \brief An implementation for a threadsafe queue using the C++11 thread library
 * for audio samples.
 */

#ifndef _SAMPLE_QUEUE_H_
#define _SAMPLE_QUEUE_H_

#define DEBUG_SAMPLE_QUEUE 0

#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <queue>
#include <cassert>
#include <sstream>
#include <cstdio>
#include <cmath>

/*! This queue is meant to be used by two threads. One producer
 * that pushes elements into the queue, and one consumer that
 * retrieves the elements.
 *
 * This queue should contain audio sample data, interleaved L/R
 * form, 2bytes per sample. Therefore, the push and pop functions
 * should always place or retrieve data in multiples of
 * bytes_per_sample * number_of_channels
 *
 * The queue has a maximum size. If this size is reached, push()
 * either blocks or ignores new data, depending on drift_compensation.
 *
 * If pop() is called but there is not enough data in the queue,
 * the missing samples are replaced by zeros. pop() will always
 * write the requested length.
 */


/* The template is actually not really tested for anything else
 * than uint8_t
 */
template<typename T>
class SampleQueue
{
public:
    SampleQueue(unsigned int bytes_per_sample) :
        m_bytes_per_sample(bytes_per_sample) {}

    void configure(size_t max_size, bool push_block, unsigned int channels)
    {
        m_max_size = max_size;
        m_push_block = push_block;
        m_channels = channels;
    }


    /*! Push a bunch of samples into the buffer
     *
     * \return size of the queue after the push
     */
    size_t push(const T *val, size_t len)
    {
        size_t new_size = 0;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            assert(len % (m_channels * m_bytes_per_sample) == 0);

#if DEBUG_SAMPLE_QUEUE
            fprintf(stdout, "######## push %s %zu, %zu >= %zu\n",
                    (m_queue.size() >= m_max_size) ? "overrun" : "ok",
                    len / 4,
                    m_queue.size() / 4,
                    m_max_size / 4);
#endif

            if (m_push_block) {
                while (len) {
                    const size_t available = m_max_size - m_queue.size();
                    const size_t copy_len = std::min(available, len);

                    if (copy_len > 0) {
                        std::copy(val, val + copy_len, std::back_inserter(m_queue));
                        len -= copy_len;
                        val += copy_len;
                    }
                    else {
                        const auto wait_timeout = std::chrono::milliseconds(100);
                        m_pop_notification.wait_for(lock, wait_timeout);
                    }
                }
            }
            else {
                if (m_queue.size() < m_max_size) {
                    std::copy(val, val + len, std::back_inserter(m_queue));
                }
                else {
                    m_overruns++;
                }
            }

            new_size = m_queue.size();
        }

        m_push_notification.notify_all();

        return new_size;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    /*! Wait until len elements in the queue are available,
     * and then fill the buf. If the timeout_ms (expressed in milliseconds
     * expires), fill the available number of elements.
     * Also update the overrun variable with the information
     * of how many overruns we saw since the last pop.
     *
     * \return the number of elemets written into buf
     */
    size_t pop_wait(T* buf, size_t len, int timeout_ms, size_t* overruns = NULL)
    {
        assert(len % (m_channels * m_bytes_per_sample) == 0);

#if DEBUG_SAMPLE_QUEUE
        fprintf(stdout, "######## pop_wait %zu\n", len);
#endif
        std::unique_lock<std::mutex> lock(m_mutex);

        if (overruns) {
            *overruns = m_overruns;
            m_overruns = 0;
        }

        auto time_start = std::chrono::steady_clock::now();
        const auto timeout = std::chrono::milliseconds(timeout_ms);

        do {
            const auto wait_timeout = std::chrono::milliseconds(10);
            m_push_notification.wait_for(lock, wait_timeout);

#if DEBUG_SAMPLE_QUEUE
                fprintf(stdout, "######## pop_wait %zu need %zu\n",
                        m_queue.size(), len);
#endif

            if (std::chrono::steady_clock::now() - time_start > timeout) {
#if DEBUG_SAMPLE_QUEUE
                fprintf(stdout, "######## pop_wait timeout\n");
#endif
                break;
            }
        } while (m_queue.size() < len);

        size_t num_to_copy = (m_queue.size() < len) ?
            m_queue.size() : len;

        std::copy(
                m_queue.begin(),
                m_queue.begin() + num_to_copy,
                buf);

        m_queue.erase(m_queue.begin(), m_queue.begin() + num_to_copy);

        lock.unlock();

#if DEBUG_SAMPLE_QUEUE
        fprintf(stdout, "######## pop_wait returns %zu\n", num_to_copy);
#endif

        m_pop_notification.notify_all();
        return num_to_copy;
    }

    /*! Get up to len elements, place them into the buf array
     *
     * \return the number of elements it was able to take
     * from the queue
     */
    size_t pop(T* buf, size_t len)
    {
        size_t ovr;
        return pop(buf, len, ovr);
    }

    /*! Get up to len elements, place them into the buf array.
     * Also update the overrun variable with the information
     * of how many overruns we saw since the last pop.
     *
     * \return the number of elements it was able to take
     * from the queue
     */
    size_t pop(T* buf, size_t len, size_t* overruns)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        assert(len % (m_channels * m_bytes_per_sample) == 0);

#if DEBUG_SAMPLE_QUEUE
        fprintf(stdout, "######## pop %zu (%zu), %zu overruns: ",
                len / 4,
                m_queue.size() / 4,
                m_overruns);
#endif
        *overruns = m_overruns;
        m_overruns = 0;

        size_t ret = 0;

        if (m_queue.size() < len) {
            /* Not enough data in queue, fill with zeros */

            size_t i;
            for (i = 0; i < m_queue.size(); i++) {
                buf[i] = m_queue[i];
            }

            ret = i;

            for (; i < len; i++) {
                buf[i] = 0;
            }

            m_queue.resize(0);

#if DEBUG_SAMPLE_QUEUE
            fprintf(stdout, "after short pop %zu (%zu)\n",
                len / 4,
                m_queue.size() / 4);
#endif
        }
        else {
            /* Queue contains enough data */

            for (size_t i = 0; i < len; i++) {
                buf[i] = m_queue[i];
            }

            ret = len;

            m_queue.erase(m_queue.begin(), m_queue.begin() + len);

#if DEBUG_SAMPLE_QUEUE
            fprintf(stdout, "after ok pop %zu (%zu)\n",
                len / 4,
                m_queue.size() / 4);
#endif
        }

        m_pop_notification.notify_all();
        return ret;
    }

    void clear()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.clear();
#if DEBUG_SAMPLE_QUEUE
            fprintf(stdout, "clear\n");
#endif
        }
        m_pop_notification.notify_all();
    }

private:
    std::deque<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_push_notification;
    std::condition_variable m_pop_notification;

    unsigned int m_bytes_per_sample;
    unsigned int m_channels = 2;
    size_t m_max_size = 1;
    bool m_push_block = true;

    /*! Counter to keep track of number of overruns between calls
     * to pop()
     */
    size_t m_overruns = 0;
};

#endif

