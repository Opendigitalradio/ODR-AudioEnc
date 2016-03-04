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
#include <cstring>
#include <chrono>
#include <functional>

#include "VLCInput.h"

#include "config.h"

#if HAVE_VLC

#include <sys/time.h>

int check_vlc_uses_size_t();

using namespace std;

// VLC Audio prerender callback
void prepareRender_size_t(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        size_t size)
{
    VLCInput* in = (VLCInput*)p_audio_data;

    in->preRender_cb(pp_pcm_buffer, size);
}

void prepareRender(
        void* p_audio_data,
        uint8_t** pp_pcm_buffer,
        unsigned int size)
{
    VLCInput* in = (VLCInput*)p_audio_data;

    in->preRender_cb(pp_pcm_buffer, size);
}


// Audio postrender callback
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

    assert(channels == in->getChannels());
    assert(rate == in->getRate());
    assert(bits_per_sample == 8*BYTES_PER_SAMPLE);

    // This assumes VLC always gives back the full
    // buffer it asked for. According to VLC code
    // smem.c for v2.2.0 this holds.
    in->postRender_cb();
}

// convert from unsigned int size to size_t size
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

// VLC Exit callback
void handleVLCExit(void* opaque)
{
    ((VLCInput*)opaque)->exit_cb();
}

int VLCInput::prepare()
{
    int err;
    fprintf(stderr, "Initialising VLC...\n");

    long long int handleStream_address;
    long long int prepareRender_address;

    int vlc_version_check = check_vlc_uses_size_t();
    if (vlc_version_check == 0) {
        fprintf(stderr, "You are using VLC with unsigned int size callbacks\n");

        handleStream_address = (long long int)(intptr_t)(void*)&handleStream;
        prepareRender_address = (long long int)(intptr_t)(void*)&prepareRender;
    }
    else if (vlc_version_check == 1) {
        fprintf(stderr, "You are using VLC with size_t size callbacks\n");

        handleStream_address = (long long int)(intptr_t)(void*)&handleStream_size_t;
        prepareRender_address = (long long int)(intptr_t)(void*)&prepareRender_size_t;
    }
    else {
        fprintf(stderr, "Error detecting VLC version!\n");
        fprintf(stderr, "      you are using %s\n", libvlc_get_version());
        return -1;
    }


    // VLC options
    std::stringstream transcode_options_ss;
    transcode_options_ss << "acodec=s16l";
    transcode_options_ss << ",samplerate=" << m_rate;
    if (not m_gain.empty()) {
        transcode_options_ss << ",afilter=compressor";
    }
    string transcode_options = transcode_options_ss.str();

    char smem_options[512];
    snprintf(smem_options, sizeof(smem_options),
            "#transcode{%s}:"
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

#define VLC_ARGS_LEN 32
    const char* vlc_args[VLC_ARGS_LEN];
    size_t arg_ix = 0;
    std::stringstream arg_verbose;
    arg_verbose << "--verbose=" << m_verbosity;
    vlc_args[arg_ix++] = arg_verbose.str().c_str();

    std::string arg_network_caching;
    if (not m_cache.empty()) {
        stringstream ss;
        ss << "--network-caching=" << m_cache;
        arg_network_caching = ss.str();
        vlc_args[arg_ix++] = arg_network_caching.c_str();
    }

    std::string arg_gain;
    if (not m_gain.empty()) {
        stringstream ss;
        ss << "--compressor-makeup=" << m_gain;
        arg_gain = ss.str();
        vlc_args[arg_ix++] = arg_gain.c_str();
    }

    vlc_args[arg_ix++] = "--sout";
    vlc_args[arg_ix++] = smem_options; // Stream to memory

    for (const auto& opt : m_additional_opts) {
        if (arg_ix < VLC_ARGS_LEN) {
            vlc_args[arg_ix++] = opt.c_str();
        }
        else {
            fprintf(stderr, "Too many VLC options given");
            return 1;
        }
    }

    if (m_verbosity) {
        fprintf(stderr, "Initialising VLC with options:\n");
        for (size_t i = 0; i < arg_ix; i++) {
            fprintf(stderr, "  %s\n", vlc_args[i]);
        }
    }

    // Launch VLC
    m_vlc = libvlc_new(arg_ix, vlc_args);

    libvlc_set_exit_handler(m_vlc, handleVLCExit, this);

    // Load the media
    libvlc_media_t *m;
    m = libvlc_media_new_location(m_vlc, m_uri.c_str());
    m_mp = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);

    // Start playing
    int ret = libvlc_media_player_play(m_mp);

    if (ret == 0) {
        libvlc_media_t *media = libvlc_media_player_get_media(m_mp);
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
    }

    return ret;
}

void VLCInput::preRender_cb(uint8_t** pp_pcm_buffer, size_t size)
{
    const size_t max_length = 20 * size;

    for (;;) {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);

            if (m_queue.size() < max_length) {
                m_current_buf.resize(size);
                *pp_pcm_buffer = &m_current_buf[0];
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void VLCInput::exit_cb()
{
    std::lock_guard<std::mutex> lock(m_queue_mutex);

    fprintf(stderr, "VLC exit, restarting...\n");

    cleanup();
    m_current_buf.clear();
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

void VLCInput::postRender_cb()
{
    std::lock_guard<std::mutex> lock(m_queue_mutex);

    size_t queue_size = m_queue.size();
    m_queue.resize(m_queue.size() + m_current_buf.size());
    std::copy(m_current_buf.begin(), m_current_buf.end(),
            m_queue.begin() + queue_size);
}

ssize_t VLCInput::m_read(uint8_t* buf, size_t length)
{
    ssize_t err = 0;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);

            if (m_queue.size() >= length) {
                std::copy(m_queue.begin(), m_queue.begin() + length, buf);

                m_queue.erase(m_queue.begin(), m_queue.begin() + length);

                return length;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        libvlc_media_t *media = libvlc_media_player_get_media(m_mp);
        libvlc_state_t st = libvlc_media_get_state(media);
        if (!(st == libvlc_Opening   ||
              st == libvlc_Buffering ||
              st == libvlc_Playing) ) {
            fprintf(stderr, "VLC state is %d\n", st);
            err = -1;
            break;
        }

        char* nowplaying_sz = libvlc_media_get_meta(media, libvlc_meta_NowPlaying);
        if (nowplaying_sz) {
            m_nowplaying = nowplaying_sz;
            free(nowplaying_sz);
        }
    }
    return err;
}

ssize_t VLCInputDirect::read(uint8_t* buf, size_t length)
{
    int bytes_per_frame = m_channels * BYTES_PER_SAMPLE;
    assert(length % bytes_per_frame == 0);

    ssize_t read = m_read(buf, length);

    return read;
}

bool write_icy_to_file(const std::string& text, const std::string& filename)
{
    FILE* fd = fopen(filename.c_str(), "wb");
    if (fd) {
        int ret = fputs(text.c_str(), fd);
        fclose(fd);

        return ret >= 0;
    }

    return false;
}

void VLCInput::write_icy_text(const std::string& filename)
{
    if (icy_text_written.valid()) {
        auto status = icy_text_written.wait_for(std::chrono::microseconds(1));
        if (status == std::future_status::ready) {
            if (not icy_text_written.get()) {
                fprintf(stderr, "Failed to write ICY Text to file!\n");
            }
        }
    }

    else {
        if (m_nowplaying_previous != m_nowplaying) {
            icy_text_written = std::async(std::launch::async,
                    std::bind(write_icy_to_file, m_nowplaying, filename));

        }

        m_nowplaying_previous = m_nowplaying;
    }
}


// ==================== VLCInputThreaded ====================

void VLCInputThreaded::start()
{
    if (m_fault) {
        fprintf(stderr, "Cannot start VLC input. Fault detected previsouly!\n");
    }
    else {
        m_running = true;
        m_thread = std::thread(&VLCInputThreaded::process, this);
    }
}

// How many samples we insert into the queue each call
// 10 samples @ 32kHz = 3.125ms
#define NUM_BYTES_PER_CALL (10 * BYTES_PER_SAMPLE)

void VLCInputThreaded::process()
{
    uint8_t samplebuf[NUM_BYTES_PER_CALL];
    while (m_running) {
        ssize_t n = m_read(samplebuf, NUM_BYTES_PER_CALL);

        if (n < 0) {
            m_running = false;
            m_fault = true;
            break;
        }

        m_queue.push(samplebuf, n);
    }
}





/* VLC up to version 2.1.0 used a different callback function signature.
 * VLC 2.2.0 uses size_t
 *
 * \return 1 if the callback with size_t size should be used.
 *         0 if the callback with unsigned int size should be used.
 *        -1 if there was an error.
 */
int check_vlc_uses_size_t()
{
    int retval = -1;

    char libvlc_version[256];
    strncpy(libvlc_version, libvlc_get_version(), 256);

    char *space_position = strstr(libvlc_version, " ");

    if (space_position) {
        *space_position = '\0';
    }

    char *saveptr;
    char *major_ver_sz = strtok_r(libvlc_version, ".", &saveptr);
    if (major_ver_sz) {
        int major_ver = atoi(major_ver_sz);

        char *minor_ver_sz = strtok_r(NULL, ".", &saveptr);
        if (minor_ver_sz) {
            int minor_ver = atoi(minor_ver_sz);

            retval = (major_ver >= 2 && minor_ver >= 2) ? 1 : 0;
        }
    }

    return retval;
}


#endif // HAVE_VLC

