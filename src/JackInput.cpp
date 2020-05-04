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

#include <cstdio>
#include <string>
#include "config.h"

#if HAVE_JACK

extern "C" {
#include <jack/jack.h>
}

#include "JackInput.h"
#include <sys/time.h>

using namespace std;

JackInput::~JackInput()
{
    // Ensures push() doesn't get blocked
    m_queue.clear();

    if (m_client) {
        jack_client_close(m_client);
    }
}

void JackInput::prepare()
{
    jack_options_t options = JackNullOption;
    jack_status_t status;
    const char *server_name = NULL;

    m_client = jack_client_open(m_jack_name.c_str(), options, &status, server_name);
    if (m_client == NULL) {
        if (status & JackServerFailed) {
            throw runtime_error("Unable to connect to JACK server");
        }
        else {
            throw runtime_error("jack_client_open() failed, status = " +
                    to_string(status));
        }
    }

    if (status & JackServerStarted) {
        fprintf(stderr, "JACK server started\n");
    }

    if (status & JackNameNotUnique) {
        throw runtime_error("JACK name '" + m_jack_name + "' not unique!");
    }

    /* Set up real-time process callback */
    jack_set_process_callback(m_client, process_cb, this);

    /* tell the JACK server to call `shutdown_cb' if
       it ever shuts down, either entirely, or if it
       just decides to stop calling us. */
    jack_on_shutdown(m_client, shutdown_cb, this);

    if (m_rate != jack_get_sample_rate(m_client)) {
        throw runtime_error(
                "JACK uses different sample_rate " +
                to_string(jack_get_sample_rate(m_client)) +
                " than requested (" + to_string(m_rate) + ")!");
    }

    /* create ports */
    for (unsigned int i = 0; i < m_channels; i++) {
        std::stringstream port_name;
        port_name << "input" << i;

        jack_port_t* input_port = jack_port_register(m_client,
                port_name.str().c_str(),
                JACK_DEFAULT_AUDIO_TYPE,
                JackPortIsInput,
                0);

        if (input_port == NULL) {
            throw runtime_error("no more JACK ports available");
        }

        m_input_ports.push_back(input_port);
    }

    /* Tell the JACK server that we are ready to roll. Our
     * process() callback will start running now. */
    if (jack_activate(m_client)) {
        throw runtime_error("JACK: cannot activate client");
    }
}

bool JackInput::read_source(size_t num_bytes)
{
    // Reading done in separate thread, no normal termination condition possible
    return true;
}

void JackInput::jack_process(jack_nframes_t nframes)
{
    /*! JACK works with float samples, we need to convert
     * them to shorts first. This is done using a saturated
     * conversion to avoid glitches.
     */
    std::vector<int16_t> buffer(m_channels * nframes);

    for (unsigned int chan = 0; chan < m_channels; chan++) {
        // start offset interleaving
        int i = chan;

        const int dst_skip = m_channels;

        jack_default_audio_sample_t* src =
            (jack_default_audio_sample_t*)jack_port_get_buffer(m_input_ports[chan], nframes);

        jack_nframes_t n = nframes;
        while (n--) {
            if (*src <= -1.0f) {
                buffer[i] = 32767;
            }
            else if (*src >= 1.0f) {
                buffer[i] = -32768;
            }
            else {
                buffer[i] =
                    (int16_t)lrintf(*src * 32768.0f);
            }

            i += dst_skip;
            src++;
        }
    }

    m_queue.push((uint8_t*)&buffer.front(), buffer.size() * sizeof(uint16_t));
}

#endif // HAVE_JACK

