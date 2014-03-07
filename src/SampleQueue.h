/*
   Copyright (C) 2013, 2014
   Matthias P. Braendli, matthias.braendli@mpb.li

   An implementation for a threadsafe queue using boost thread library
   for audio samples.
*/

#ifndef _SAMPLE_QUEUE_H_
#define _SAMPLE_QUEUE_H_

#include <boost/thread.hpp>
#include <queue>

/* This queue is meant to be used by two threads. One producer
 * that pushes elements into the queue, and one consumer that
 * retrieves the elements.
 *
 * The queue can make the consumer block until enough elements
 * are available.
 */

template<typename T>
class SampleQueue
{
public:
    SampleQueue(unsigned int bytes_per_sample,
            unsigned int channels,
            size_t max_size) :
        m_bytes_per_sample(bytes_per_sample),
        m_channels(channels), m_max_size(max_size) {}


    /* Push a bunch of samples into the buffer */
    size_t push(const T *val, size_t len)
    {
        boost::mutex::scoped_lock lock(m_mutex);

        if (m_queue.size() >= m_max_size) {
            return 0;
        }

        for (size_t i = 0; i < len; i++) {
            m_queue.push_back(val[i]);
        }

        size_t new_size = m_queue.size();
        lock.unlock();

        //m_condition_variable.notify_one();

        return new_size;
    }

    bool empty() const
    {
        boost::mutex::scoped_lock lock(m_mutex);
        return m_queue.empty();
    }

    /* Get len elements, place them into the buf array
     * Returns the number of elements it was able to take
     * from the queue
     */
    size_t pop(T* buf, size_t len)
    {
        boost::mutex::scoped_lock lock(m_mutex);

        size_t ret = 0;

        if (m_queue.size() < len) {
            size_t i;
            for (i = 0; i < m_queue.size(); i++) {
                buf[i] = m_queue[i];
            }

            ret = i;

            for (; i < len; i++) {
                buf[i] = 0;
            }

            m_queue.resize(0);
        }
        else {
            for (size_t i = 0; i < len; i++) {
                buf[i] = m_queue[i];
            }

            ret = len;

            m_queue.erase(m_queue.front(), m_queue.front() + len);
        }

        return ret;
    }

    /*
    void wait_and_pop(T& popped_value)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        while(m_queue.size() < m_required_size)
        {
            m_condition_variable.wait(lock);
        }

        popped_value = m_queue.front();
        m_queue.pop_front();
    }
    */

private:
    std::deque<T> m_queue;
    mutable boost::mutex m_mutex;
    //boost::condition_variable m_condition_variable;

    unsigned int m_channels;
    unsigned int m_bytes_per_sample;
    size_t m_max_size;
};

#endif

