/* ------------------------------------------------------------------
 * Copyright (C) 2018 Matthias P. Braendli
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
#include <cstring>
#include <chrono>
#include <algorithm>
#include <functional>

#include "VLCInput.h"

#include "config.h"

#if HAVE_VLC

const size_t bytes_per_float_sample = sizeof(float);

#include <sys/time.h>

enum class vlc_data_type_e {
    vlc_uses_size_t,
    vlc_uses_unsigned_int
};

static vlc_data_type_e check_vlc_uses_size_t();

using namespace std;

/*! \note VLC callback functions have to be C functions.
 * These wrappers call the VLCInput functions
 */

//! VLC Audio prerender callback
void prepareRender_size_t(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        size_t size)
{
    VLCInput* in = (VLCInput*)p_audio_data;

    in->preRender_cb(pp_pcm_buffer, size);
}

//! VLC Audio prepare render callback
void prepareRender(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        unsigned int size)
{
    VLCInput* in = (VLCInput*)p_audio_data;

    in->preRender_cb(pp_pcm_buffer, size);
}


//! Audio postrender callback for VLC versions that use size_t
void handleStream_size_t(
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

    assert((ssize_t)rate == in->getRate());
    assert(bits_per_sample == 8*bytes_per_float_sample);

    // This assumes VLC always gives back the full
    // buffer it asked for. According to VLC code
    // smem.c for v2.2.0 this holds.
    in->postRender_cb(channels, size);
}

/*! Audio postrender callback for VLC versions that use unsigned int.
 * Convert from unsigned int size to size_t size
 */
void handleStream(
        void* p_audio_data,
        uint8_t* p_pcm_buffer,
        unsigned int channels,
        unsigned int rate,
        unsigned int nb_samples,
        unsigned int bits_per_sample,
        unsigned int size,
        int64_t pts)
{
    handleStream_size_t(
        p_audio_data,
        p_pcm_buffer,
        channels,
        rate,
        nb_samples,
        bits_per_sample,
        size,
        pts);
}

/*! VLC Exit callback */
void handleVLCExit(void* opaque)
{
    ((VLCInput*)opaque)->exit_cb();
}

VLCInput::~VLCInput()
{
    m_running = false;

    // Ensures push() doesn't get blocked
    m_samplequeue.clear();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    cleanup();
}

void VLCInput::prepare()
{
    if (m_fault) {
        throw runtime_error("Cannot start VLC input. Fault detected previously!");
    }

    fprintf(stderr, "Initialising VLC...\n");

    long long int handleStream_address;
    long long int prepareRender_address;

    switch (check_vlc_uses_size_t()) {
        case vlc_data_type_e::vlc_uses_unsigned_int:
            fprintf(stderr, "You are using VLC with unsigned int size callbacks\n");

            handleStream_address = (long long int)(intptr_t)(void*)&handleStream;
            prepareRender_address = (long long int)(intptr_t)(void*)&prepareRender;
            break;
        case vlc_data_type_e::vlc_uses_size_t:
            fprintf(stderr, "You are using VLC with size_t size callbacks\n");

            handleStream_address = (long long int)(intptr_t)(void*)&handleStream_size_t;
            prepareRender_address = (long long int)(intptr_t)(void*)&prepareRender_size_t;
            break;
    }

    // VLC options
    vector<string> vlc_args;
    vlc_args.push_back("--verbose=" + to_string(m_verbosity));

    if (not m_cache.empty()) {
        vlc_args.push_back("--network-caching=" + m_cache);
    }

    if (not m_gain.empty()) {
        vlc_args.push_back("--compressor-makeup=" + m_gain);
    }

    copy(m_additional_opts.begin(), m_additional_opts.end(),
            back_inserter(vlc_args));

    if (m_verbosity) {
        fprintf(stderr, "Initialising VLC with options:\n");
        for (const auto& arg : vlc_args) {
            fprintf(stderr, "  %s\n", arg.c_str());
        }
    }

    // Launch VLC
    vector<const char*> vlc_args_c;
    for (const auto& arg : vlc_args) {
        vlc_args_c.push_back(arg.c_str());
    }

    m_vlc = libvlc_new(vlc_args_c.size(), vlc_args_c.data());

    if (m_vlc == nullptr) {
        throw runtime_error("VLC initialisation failed");
    }

    libvlc_set_exit_handler(m_vlc, handleVLCExit, this);

    // Load the media
    libvlc_media_t *m;
    m = libvlc_media_new_location(m_vlc, m_uri.c_str());

    std::stringstream transcode_options_ss;
    transcode_options_ss << "acodec=fl32";
    transcode_options_ss << ",samplerate=" << m_rate;
    if (not m_gain.empty()) {
        transcode_options_ss << ",afilter=compressor";
    }
    string transcode_options = transcode_options_ss.str();

    char smem_options[512];
    snprintf(smem_options, 511,
            ":sout=#transcode{%s}:"
            // We are using transcode because smem only support raw audio and
            // video formats
            "smem{"
                "audio-postrender-callback=%lld,"
                "audio-prerender-callback=%lld,"
                "audio-data=%lld"
            "}",
            transcode_options.c_str(),
            handleStream_address,
            prepareRender_address,
            (long long int)(intptr_t)this);

    if (m_verbosity) {
        fprintf(stderr, "Setting VLC media option: %s\n", smem_options);
    }

    libvlc_media_add_option(m, smem_options);
    m_mp = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);

    if (!m_mp) {
        throw runtime_error("VLC did not return media player");
    }

    // Start playing
    int ret = libvlc_media_player_play(m_mp);

    if (ret == 0) {
        libvlc_media_t *media = libvlc_media_player_get_media(m_mp);
        if (!media) {
            throw runtime_error("VLC did not have a media");
        }
        libvlc_state_t st;

        ret = -1;

        for (int timeout = 0; timeout < 100; timeout++) {
            st = libvlc_media_get_state(media);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (st != libvlc_NothingSpecial) {
                ret = 0;
                break;
            }
        }

        libvlc_media_release(media);
    }

    if (ret == -1) {
        throw runtime_error("VLC input did not start playing media");
    }

    m_running = true;
    m_thread = std::thread(&VLCInput::process, this);
}

bool VLCInput::read_source(size_t num_bytes)
{
    // Reading done in separate thread, no normal termination condition possible
    return true;
}

void VLCInput::preRender_cb(uint8_t** pp_pcm_buffer, size_t size)
{
    const size_t max_length = 20 * size;

    while (true) {
        std::unique_lock<std::mutex> lock(m_queue_mutex);

        if (m_queue.size() < max_length) {
            m_current_buf.resize(size / sizeof(float));
            *pp_pcm_buffer = reinterpret_cast<uint8_t*>(&m_current_buf[0]);
            return;
        }

        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void VLCInput::exit_cb()
{
    if (m_running) {
        std::lock_guard<std::mutex> lock(m_queue_mutex);

        fprintf(stderr, "VLC exit, restarting...\n");

        cleanup();
        m_current_buf.clear();
        prepare();
    }
    else {
        fprintf(stderr, "VLC exit.\n");
    }
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
        m_vlc = nullptr;
    }
}

void VLCInput::postRender_cb(unsigned int channels, size_t size)
{
    std::lock_guard<std::mutex> lock(m_queue_mutex);

    assert(size == m_current_buf.size() * sizeof(float));

    if (channels == m_channels) {
        size_t queue_size = m_queue.size();
        m_queue.resize(m_queue.size() + m_current_buf.size());
        std::copy(m_current_buf.begin(), m_current_buf.end(),
                m_queue.begin() + queue_size);
    }
    else if (channels == 2 and m_channels == 1) {
        // Downmix to 1 channel
        for (size_t i = 0; i+1 < m_current_buf.size(); i += 2) {
            m_queue.push_back(0.5f * (m_current_buf[i] + m_current_buf[i+1]));
        }
    }
    else {
        fprintf(stderr, "Got invalid number of channels back from VLC! "
                "requested: %d, got %d\n", m_channels, channels);
        m_running = false;
        m_fault = true;
    }
}

ssize_t VLCInput::m_read(uint8_t* buf, size_t length)
{
    ssize_t err = 0;
    while (m_running) {
        std::unique_lock<std::mutex> lock(m_queue_mutex);

        assert((length % sizeof(int16_t)) == 0);
        const size_t num_samples_requested = length / sizeof(int16_t);

        // The queue contains float samples.
        // buf has to contain signed 16-bit samples.
        if (m_queue.size() >= num_samples_requested) {
            int16_t* buffer = reinterpret_cast<int16_t*>(buf);

            for (size_t i = 0; i < num_samples_requested; i++) {
                const auto in = m_queue[i];
                if (in <= -1.0f) {
                    buffer[i] = INT16_MAX;
                }
                else if (in >= 1.0f) {
                    buffer[i] = INT16_MIN;
                }
                else {
                    buffer[i] = (int16_t)lrintf(in * 32768.0f);
                }
            }

            m_queue.erase(
                    m_queue.begin(),
                    m_queue.begin() + num_samples_requested);

            return num_samples_requested * sizeof(int16_t);
        }

        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        libvlc_media_t *media = libvlc_media_player_get_media(m_mp);
        if (!media) {
            fprintf(stderr, "VLC no media\n");
            err = -1;
            break;
        }

        libvlc_state_t st = libvlc_media_get_state(media);
        if (!(st == libvlc_Opening   ||
              st == libvlc_Buffering ||
              st == libvlc_Playing) ) {
            fprintf(stderr, "VLC state is %d\n", st);
            err = -1;
            break;
        }

        // handle meta data. Warning: do not leak these!
        char* artist_sz = libvlc_media_get_meta(media, libvlc_meta_Artist);
        char* title_sz = libvlc_media_get_meta(media, libvlc_meta_Title);

        if (artist_sz and title_sz) {
            // use Artist and Title
            std::lock_guard<std::mutex> lock(m_nowplaying_mutex);
            m_nowplaying.useArtistTitle(artist_sz, title_sz);
        }
        else {
            // try fallback to NowPlaying
            char* nowplaying_sz = libvlc_media_get_meta(media,
                    libvlc_meta_NowPlaying);

            if (nowplaying_sz) {
                std::lock_guard<std::mutex> lock(m_nowplaying_mutex);
                m_nowplaying.useNowPlaying(nowplaying_sz);
                free(nowplaying_sz);
            }
        }

        libvlc_media_release(media);

        if (artist_sz)
            free(artist_sz);
        if (title_sz)
            free(title_sz);
    }
    return err;
}


ICY_TEXT_t VLCInput::get_icy_text() const
{
    ICY_TEXT_t now_playing;
    {
        std::lock_guard<std::mutex> lock(m_nowplaying_mutex);
        now_playing = m_nowplaying;
    }

    return now_playing;
}

/*! How many samples we insert into the queue each call
 * 10 samples @ 32kHz = 3.125ms
 */
#define NUM_BYTES_PER_CALL (10 * BYTES_PER_SAMPLE)

void VLCInput::process()
{
    uint8_t samplebuf[NUM_BYTES_PER_CALL];
    while (m_running) {
        ssize_t n = m_read(samplebuf, NUM_BYTES_PER_CALL);

        if (n < 0) {
            m_running = false;
            m_fault = true;
            break;
        }

        m_samplequeue.push(samplebuf, n);
    }
}





/*! VLC up to version 2.1.0 used a different callback function signature.
 * VLC 2.2.0 uses size_t
 */
vlc_data_type_e check_vlc_uses_size_t()
{
    char libvlc_version[256] = {};
    strncpy(libvlc_version, libvlc_get_version(), 255);

    char *space_position = strstr(libvlc_version, " ");

    if (space_position) {
        *space_position = '\0';
    }

    char *saveptr;
    char *major_ver_sz = strtok_r(libvlc_version, ".", &saveptr);
    if (major_ver_sz) {
        int major_ver = atoi(major_ver_sz);

        char *minor_ver_sz = strtok_r(nullptr, ".", &saveptr);
        if (minor_ver_sz) {
            int minor_ver = atoi(minor_ver_sz);

            if (major_ver > 2) {
                return vlc_data_type_e::vlc_uses_size_t;
            }
            else if (major_ver >= 2 && minor_ver >= 2) {
                return vlc_data_type_e::vlc_uses_size_t;
            }
            else {
                return vlc_data_type_e::vlc_uses_unsigned_int;
            }
        }
    }

    fprintf(stderr, "Error detecting VLC version!\n");
    fprintf(stderr, "      you are using %s\n", libvlc_get_version());
    throw runtime_error("Cannot identify VLC datatype!");
}


#endif // HAVE_VLC

