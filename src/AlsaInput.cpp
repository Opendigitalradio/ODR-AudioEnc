/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2017 Matthias P. Braendli
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

#include "config.h"
#if HAVE_ALSA

#include "AlsaInput.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <alsa/asoundlib.h>
#include <sys/time.h>

using namespace std;

AlsaInput::~AlsaInput()
{
    // Ensures push() doesn't get blocked
    m_queue.clear();

    if (m_alsa_handle) {
        snd_pcm_close(m_alsa_handle);
        m_alsa_handle = nullptr;
    }
}

static std::string alsa_strerror(int err)
{
    string s(snd_strerror(err));
    return s;
}

void AlsaInput::m_init_alsa()
{
    int err;
    snd_pcm_hw_params_t *hw_params;

    fprintf(stderr, "Initialising ALSA...\n");

    const int open_mode = 0;

    if ((err = snd_pcm_open(&m_alsa_handle, m_alsa_dev.c_str(),
                    SND_PCM_STREAM_CAPTURE, open_mode)) < 0) {
        throw runtime_error("cannot open audio device " +
                m_alsa_dev + "(" + alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        throw runtime_error("cannot allocate hardware parameter structure (" +
                alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params_any(m_alsa_handle, hw_params)) < 0) {
        throw runtime_error("cannot initialize hardware parameter structure (" +
                alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params_set_access(m_alsa_handle, hw_params,
                    SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        throw runtime_error("cannot set access type (" + alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params_set_format(m_alsa_handle, hw_params,
                    SND_PCM_FORMAT_S16_LE)) < 0) {
        throw runtime_error("cannot set sample format (" +
                alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params_set_rate_near(m_alsa_handle,
                hw_params, &m_rate, 0)) < 0) {
        throw runtime_error("cannot set sample rate (" + alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params_set_channels(m_alsa_handle,
                    hw_params, m_channels)) < 0) {
        throw runtime_error("cannot set channel count (" +
                alsa_strerror(err) + ")");
    }

    if ((err = snd_pcm_hw_params(m_alsa_handle, hw_params)) < 0) {
        throw runtime_error("cannot set parameters (" + alsa_strerror(err) + ")");
    }

    snd_pcm_hw_params_free (hw_params);

    if ((err = snd_pcm_prepare(m_alsa_handle)) < 0) {
        throw runtime_error("cannot prepare audio interface for use (" +
                alsa_strerror(err) + ")");
    }

    fprintf(stderr, "ALSA init done.\n");
}

ssize_t AlsaInput::m_read(uint8_t* buf, snd_pcm_uframes_t length)
{
    int err;

    err = snd_pcm_readi(m_alsa_handle, buf, length);

    if (err != (ssize_t)length) {
        if (err < 0) {
            fprintf (stderr, "read from audio interface failed (%s)\n",
                    snd_strerror(err));
        }
        else {
            fprintf(stderr, "short alsa read: %d\n", err);
        }
    }

    return err;
}

AlsaInputThreaded::~AlsaInputThreaded()
{
    m_running = false;

    // Ensures push() doesn't get blocked
    m_queue.clear();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void AlsaInputThreaded::prepare()
{
    if (m_fault) {
        fprintf(stderr, "Cannot start alsa input. Fault detected previsouly!\n");
    }
    else {
        m_init_alsa();

        m_running = true;
        m_thread = std::thread(&AlsaInputThreaded::process, this);
    }
}

bool AlsaInputThreaded::read_source(size_t num_bytes)
{
    // Reading done in separate thread, no normal termination condition possible
    return true;
}

void AlsaInputThreaded::process()
{
    uint8_t samplebuf[NUM_SAMPLES_PER_CALL * BYTES_PER_SAMPLE * m_channels];
    while (m_running) {
        ssize_t n = m_read(samplebuf, NUM_SAMPLES_PER_CALL);

        if (n < 0) {
            m_running = false;
            m_fault = true;
            break;
        }

        m_queue.push(samplebuf, BYTES_PER_SAMPLE*m_channels*n);
    }
}

void AlsaInputDirect::prepare()
{
    m_init_alsa();
}

bool AlsaInputDirect::read_source(size_t num_bytes)
{
    const int bytes_per_frame = m_channels * BYTES_PER_SAMPLE;
    assert(num_bytes % bytes_per_frame == 0);

    const size_t num_frames = num_bytes / bytes_per_frame;
    vector<uint8_t> buf(num_bytes);
    ssize_t ret = m_read(buf.data(), num_frames);

    if (ret > 0) {
        m_queue.push(buf.data(), ret * bytes_per_frame);
    }
    return ret == (ssize_t)num_frames;
}

#endif // HAVE_ALSA

