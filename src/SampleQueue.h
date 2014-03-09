/*
   Copyright (C) 2013, 2014
   Matthias P. Braendli, matthias.braendli@mpb.li

   An implementation for a threadsafe queue using boost thread library
   for audio samples.
*/

#ifndef _SAMPLE_QUEUE_H_
#define _SAMPLE_QUEUE_H_

#define DEBUG_SAMPLE_QUEUE 0

#include <boost/thread.hpp>
#include <queue>

#include <stdio.h>

/* This queue is meant to be used by two threads. One producer
 * that pushes elements into the queue, and one consumer that
 * retrieves the elements.
 *
 * This queue should contain audio sample data, interleaved L/R
 * form, 2bytes per sample. Therefore, the push and pop functions
 * should always place or retrieve data in multiples of
 * bytes_per_sample * number_of_channels
 *
 * The queue has a maximum size. If this size is reached, push()
 * ignores new data.
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
    SampleQueue(unsigned int bytes_per_sample,
            unsigned int channels,
            size_t max_size) :
        m_bytes_per_sample(bytes_per_sample),
        m_channels(channels),
        m_max_size(max_size),
        m_overruns(0) {}


    /* Push a bunch of samples into the buffer */
    size_t push(const T *val, size_t len)
    {
        boost::mutex::scoped_lock lock(m_mutex);

        assert(len % (m_channels * m_bytes_per_sample) == 0);

#if DEBUG_SAMPLE_QUEUE
        fprintf(stdout, "######## push %s %zu, %zu >= %zu\n",
                (m_queue.size() >= m_max_size) ? "overrun" : "ok",
                len / 4,
                m_queue.size() / 4,
                m_max_size / 4);
#endif

        if (m_queue.size() >= m_max_size) {
            m_overruns++;
            return 0;
        }

        for (size_t i = 0; i < len; i++) {
            m_queue.push_back(val[i]);
        }

        size_t new_size = m_queue.size();

        return new_size;
    }

    size_t size() const
    {
        boost::mutex::scoped_lock lock(m_mutex);
        return m_queue.size();
    }

    /* Get len elements, place them into the buf array
     * Returns the number of elements it was able to take
     * from the queue
     */
    size_t pop(T* buf, size_t len)
    {
        size_t ovr;
        return pop(buf, len, ovr);
    }

    /* Get len elements, place them into the buf array.
     * Also update the overrun variable with the information
     * of how many overruns we saw since the last pop.
     * Returns the number of elements it was able to take
     * from the queue
     */
    size_t pop(T* buf, size_t len, size_t* overruns)
    {
        boost::mutex::scoped_lock lock(m_mutex);

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

        return ret;
    }

private:
    std::deque<T> m_queue;
    mutable boost::mutex m_mutex;

    unsigned int m_channels;
    unsigned int m_bytes_per_sample;
    size_t m_max_size;

    /* Counter to keep track of number of overruns between calls
     * to pop()
     */
    size_t m_overruns;
};

#endif

