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

#include <string>
#include <chrono>
#include <algorithm>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <gst/audio/audio.h>

#include "GSTInput.h"

#include "config.h"

#if HAVE_GST

using namespace std;

GSTData::GSTData(SampleQueue<uint8_t>& samplequeue) :
    samplequeue(samplequeue)
{ }

GSTInput::GSTInput(const std::string& uri,
        int rate,
        unsigned channels,
        SampleQueue<uint8_t>& queue) :
    m_uri(uri),
    m_channels(channels),
    m_rate(rate),
    m_gst_data(queue),
    m_samplequeue(queue)
{ }

static void error_cb(GstBus *bus, GstMessage *msg, GSTData *data)
{
    GError *err;
    gchar *debug_info;

    /* Print error details on the screen */
    gst_message_parse_error(msg, &err, &debug_info);
    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
    g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
    g_clear_error(&err);
    g_free(debug_info);

    g_main_loop_quit(data->main_loop);
}

static void cb_newpad(GstElement *decodebin, GstPad *pad, GSTData *data)
{
    /* only link once */
    GstPad *audiopad = gst_element_get_static_pad(data->audio_convert, "sink");
    if (GST_PAD_IS_LINKED(audiopad)) {
        g_object_unref(audiopad);
        return;
    }

    /* check media type */
    GstCaps *caps = gst_pad_query_caps(pad, NULL);
    GstStructure *str = gst_caps_get_structure(caps, 0);
    if (!g_strrstr(gst_structure_get_name(str), "audio")) {
        gst_caps_unref(caps);
        gst_object_unref(audiopad);
        return;
    }
    gst_caps_unref(caps);

    gst_pad_link(pad, audiopad);

    g_object_unref(audiopad);
}

static GstFlowReturn new_sample (GstElement *sink, GSTData *data) {
    GstSample *sample;
    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);

        GstMapInfo map;
        gst_buffer_map(buffer, &map, GST_MAP_READ);

        data->samplequeue.push(map.data, map.size);

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);

        return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
}

void GSTInput::prepare()
{
    gst_init(nullptr, nullptr);

    m_gst_data.uridecodebin = gst_element_factory_make("uridecodebin", "uridecodebin");
    assert(m_gst_data.uridecodebin != nullptr);
    g_object_set(m_gst_data.uridecodebin, "uri", m_uri.c_str(), nullptr);
    g_signal_connect(m_gst_data.uridecodebin, "pad-added", G_CALLBACK(cb_newpad), &m_gst_data);

    m_gst_data.audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    assert(m_gst_data.audio_convert != nullptr);

    m_gst_data.caps_filter = gst_element_factory_make("capsfilter", "caps_filter");
    assert(m_gst_data.caps_filter != nullptr);

    GstAudioInfo info;
    gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, m_rate, m_channels, NULL);
    GstCaps *audio_caps = gst_audio_info_to_caps(&info);
    g_object_set(m_gst_data.caps_filter, "caps", audio_caps, NULL);

    m_gst_data.app_sink = gst_element_factory_make("appsink", "app_sink");
    assert(m_gst_data.app_sink != nullptr);

    m_gst_data.pipeline = gst_pipeline_new("pipeline");
    assert(m_gst_data.pipeline != nullptr);

    g_object_set(m_gst_data.app_sink, "emit-signals", TRUE, "caps", audio_caps, NULL);
    g_signal_connect(m_gst_data.app_sink, "new-sample", G_CALLBACK(new_sample), &m_gst_data);
    gst_caps_unref(audio_caps);

    gst_bin_add_many(GST_BIN(m_gst_data.pipeline),
            m_gst_data.uridecodebin,
            m_gst_data.audio_convert,
            m_gst_data.caps_filter,
            m_gst_data.app_sink, NULL);

    if (gst_element_link_many(
                m_gst_data.audio_convert,
                m_gst_data.caps_filter,
                m_gst_data.app_sink, NULL) != true) {
        throw runtime_error("Could not link GST elements");
    }

    m_gst_data.bus = gst_element_get_bus(m_gst_data.pipeline);
    gst_bus_add_signal_watch(m_gst_data.bus);
    g_signal_connect(G_OBJECT(m_gst_data.bus), "message::error", (GCallback)error_cb, &m_gst_data);

    gst_element_set_state(m_gst_data.pipeline, GST_STATE_PLAYING);
}

bool GSTInput::read_source(size_t num_bytes)
{
    // Reading done in glib main loop
    GstMessage *msg = gst_bus_pop_filtered(m_gst_data.bus, GST_MESSAGE_EOS);

    if (msg) {
        gst_message_unref(msg);
        return false;
    }
    return true;
}

GSTInput::~GSTInput()
{
    fprintf(stderr, "<<<<<<<<<<<<<<<<<<<< DTOR\n");

    if (m_gst_data.bus) {
        gst_object_unref(m_gst_data.bus);
    }

    if (m_gst_data.pipeline) {
        gst_element_set_state(m_gst_data.pipeline, GST_STATE_NULL);
        gst_object_unref(m_gst_data.pipeline);
    }
}

#endif // HAVE_GST
