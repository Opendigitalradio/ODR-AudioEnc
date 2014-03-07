/* ------------------------------------------------------------------
 * Copyright (C) 2011 Martin Storsjo
 * Copyright (C) 2013,2014 Matthias P. Braendli
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
#include <cstdint>
#include <string>

#include <alsa/asoundlib.h>

#include "AlsaInput.h"

using namespace std;

int AlsaInput::prepare()
{
    int err;
    snd_pcm_hw_params_t *hw_params;

    fprintf(stderr, "Initialising ALSA...\n");

    const int open_mode = 0; //|= SND_PCM_NONBLOCK;

    if ((err = snd_pcm_open(&m_alsa_handle, alsa_dev,
                    SND_PCM_STREAM_CAPTURE, open_mode)) < 0) {
        fprintf (stderr, "cannot open audio device %s (%s)\n",
                alsa_dev, snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_any(m_alsa_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_access(m_alsa_handle, hw_params,
                    SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf (stderr, "cannot set access type (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_format(m_alsa_handle, hw_params,
                    SND_PCM_FORMAT_S16_LE)) < 0) {
        fprintf (stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_rate_near(m_alsa_handle,
                hw_params, &m_rate, 0)) < 0) {
        fprintf (stderr, "cannot set sample rate (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params_set_channels(m_alsa_handle,
                    hw_params, m_channels)) < 0) {
        fprintf (stderr, "cannot set channel count (%s)\n",
                snd_strerror(err));
        return 1;
    }

    if ((err = snd_pcm_hw_params(m_alsa_handle, hw_params)) < 0) {
        fprintf (stderr, "cannot set parameters (%s)\n",
                snd_strerror(err));
        return 1;
    }

    snd_pcm_hw_params_free (hw_params);

    if ((err = snd_pcm_prepare(m_alsa_handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                snd_strerror(err));
        return 1;
    }

    fprintf(stderr, "ALSA init done.\n");
    return 0;
}

size_t AlsaInput::read(uint8_t* buf, snd_pcm_uframes_t length)
{
    int i;
    int err;

    err = snd_pcm_readi(m_alsa_handle, buf, length);

    if (err != length) {
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

int AlsaInput::start()
{
    m_running = true;
    m_thread = boost::thread(&AlsaInput::process, this);
}

void AlsaInput::process()
{
    uint8_t samplebuf[NUM_SAMPLES_PER_CALL * BYTES_PER_SAMPLE * m_channels];
    while (m_running) {
        size_t n = read(samplebuf, NUM_SAMPLES_PER_CALL);

        m_queue.push(samplebuf, n);
    }
}

