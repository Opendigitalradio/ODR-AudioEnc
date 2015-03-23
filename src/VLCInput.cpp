/* ------------------------------------------------------------------
 * Copyright (C) 2015 Matthias P. Braendli
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

#include <cstdio>
#include <string>

#include "VLCInput.h"

#include "config.h"

#if HAVE_VLC

#include <sys/time.h>
#include <boost/date_time/posix_time/posix_time.hpp>


using namespace std;

// VLC Audio prerender callback
void prepareRender(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        size_t size)
{
    VLCInput* in = (VLCInput*)p_audio_data;

    in->preRender_cb(pp_pcm_buffer, size);
}


// Audio postrender callback
void handleStream(
        void* p_audio_data,
        uint8_t* p_pcm_buffer,
        unsigned int channels,
        unsigned int rate,
        unsigned int nb_samples,
        unsigned int bits_per_sample,
        size_t size,
        int64_t pts)
{
    VLCInput* in = (VLCInput*)p_audio_data;

    assert(channels == in->getChannels());
    assert(rate == in->getRate());
    assert(bits_per_sample == 8*BYTES_PER_SAMPLE);

    in->postRender_cb(p_pcm_buffer, size);
}

// VLC Exit callback
void handleVLCExit(void* opaque)
{
    ((VLCInput*)opaque)->exit_cb();
}

int VLCInput::prepare()
{
    int err;
    fprintf(stderr, "Initialising VLC...\n");

    // VLC options
    char smem_options[512];
    snprintf(smem_options, sizeof(smem_options),
            "#transcode{acodec=s16l,samplerate=%d}:"
            // We are using transcode because smem only support raw audio and
            // video formats
            "smem{"
                "audio-postrender-callback=%lld,"
                "audio-prerender-callback=%lld,"
                "audio-data=%lld"
            "}",
            m_rate,
            (long long int)(intptr_t)(void*)&handleStream,
            (long long int)(intptr_t)(void*)&prepareRender,
            (long long int)(intptr_t)this);

    char verb_options[512];
    snprintf(verb_options, sizeof(verb_options),
            "--verbose=%d", m_verbosity);

    const char * const vlc_args[] = {
        verb_options,
        "--sout", smem_options // Stream to memory
    };

    // Launch VLC
    m_vlc = libvlc_new(sizeof(vlc_args) / sizeof(vlc_args[0]), vlc_args);

    libvlc_set_exit_handler(m_vlc, handleVLCExit, this);

    // Load the media
    libvlc_media_t *m;
    m = libvlc_media_new_location(m_vlc, m_uri.c_str());
    m_mp = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);

    // Start playing
    libvlc_media_player_play(m_mp);

    fprintf(stderr, "VLC launched.\n");
    return 0;
}

void VLCInput::preRender_cb(uint8_t** pp_pcm_buffer, size_t size)
{
    const size_t max_length = 20 * size;

    for (;;) {
        boost::mutex::scoped_lock lock(m_queue_mutex);

        if (m_queue.size() < max_length) {
            m_current_buf.resize(size);
            *pp_pcm_buffer = &m_current_buf[0];
            return;
        }

        lock.unlock();
        boost::this_thread::sleep(boost::posix_time::milliseconds(1));
    }
}

void VLCInput::exit_cb()
{
    boost::mutex::scoped_lock lock(m_queue_mutex);

    fprintf(stderr, "VLC exit, restarting...\n");

    cleanup();
    m_current_buf.empty();
    prepare();
}

void VLCInput::cleanup()
{
    if (m_mp) {
        /* Stop playing */
        libvlc_media_player_stop(m_mp);

        /* Free the media_player */
        libvlc_media_player_release(m_mp);
    }

    if (m_vlc) {
        libvlc_release(m_vlc);
        m_vlc = NULL;
    }
}

void VLCInput::postRender_cb(uint8_t* p_pcm_buffer, size_t size)
{
    boost::mutex::scoped_lock lock(m_queue_mutex);

    if (m_current_buf.size() != size) {
        fprintf(stderr,
                "Received buffer size is not equal allocated "
                "buffer size: %zu vs %zu\n",
                m_current_buf.size(), size);
    }

    size_t queue_size = m_queue.size();
    m_queue.resize(m_queue.size() + size);
    std::copy(m_current_buf.begin(), m_current_buf.end(),
            m_queue.begin() + queue_size);
}

ssize_t VLCInput::m_read(uint8_t* buf, size_t length)
{
    ssize_t err = 0;
    for (;;) {
        boost::mutex::scoped_lock lock(m_queue_mutex);

        if (m_queue.size() >= length) {
            std::copy(m_queue.begin(), m_queue.begin() + length, buf);

            m_queue.erase(m_queue.begin(), m_queue.begin() + length);

            return length;
        }

        lock.unlock();
        boost::this_thread::sleep(boost::posix_time::milliseconds(1));
    }
    return err;
}

ssize_t VLCInput::read(uint8_t* buf, size_t length)
{
    int bytes_per_frame = m_channels * BYTES_PER_SAMPLE;
    assert(length % bytes_per_frame == 0);

    ssize_t read = m_read(buf, length);

    return read;
}

#endif // HAVE_VLC

