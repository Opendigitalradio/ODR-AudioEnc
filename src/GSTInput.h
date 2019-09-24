/* ------------------------------------------------------------------
 * Copyright (C) 2019 Matthias P. Braendli
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
/*! \file GSTInput.h
 *
 * This input uses GStreamer to get audio data.
 */

#pragma once

#include "config.h"

#if HAVE_GST

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

#include <gst/gst.h>

#include "SampleQueue.h"
#include "common.h"
#include "InputInterface.h"

extern "C" {
#include "utils.h"
}

struct GSTData {
    GSTData(SampleQueue<uint8_t>& samplequeue);

    GstElement *pipeline = nullptr;
    GstElement *uridecodebin = nullptr;
    GstElement *audio_convert = nullptr;
    GstElement *caps_filter = nullptr;
    GstElement *app_sink = nullptr;

    GstBus *bus = nullptr;
    GMainLoop *main_loop = nullptr;

    SampleQueue<uint8_t>& samplequeue;
};

class GSTInput : public InputInterface
{
    public:
        GSTInput(const std::string& uri,
                 int rate,
                 unsigned channels,
                 SampleQueue<uint8_t>& queue);

        GSTInput(const GSTInput& other) = delete;
        GSTInput& operator=(const GSTInput& other) = delete;
        virtual ~GSTInput();

        virtual void prepare() override;

        virtual bool read_source(size_t num_bytes) override;

        int getRate() { return m_rate; }

        virtual bool fault_detected(void) const override { return false; };
    private:
        std::string m_uri;
        unsigned m_channels;
        int m_rate;

        GSTData m_gst_data;

        SampleQueue<uint8_t>& m_samplequeue;
};

#endif // HAVE_GST

