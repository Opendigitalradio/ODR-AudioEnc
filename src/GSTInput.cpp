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

#include "GSTInput.h"

#include "config.h"

#if HAVE_GST

#include <gst/audio/audio.h>
#include <gst/app/gstappsink.h>

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
    m_gst_data(queue)
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

static GstFlowReturn new_sample(GstElement *sink, GSTData *data) {
    /* Retrieve the buffer */
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
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

    m_gst_data.audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    assert(m_gst_data.audio_resample != nullptr);
    g_object_set(m_gst_data.audio_resample,
#if (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR >= 10) || GST_VERSION_MAJOR > 1
            "sinc-filter-mode", GST_AUDIO_RESAMPLER_FILTER_MODE_FULL,
#else
#warning "GStreamer version is too old to set GST_AUDIO_RESAMPLER_FILTER_MODE_FULL" GST_VERSION_MAJOR
#endif
            "quality", 6, // between 0 and 10, 10 being best
            /* default audio-resampler-method: GST_AUDIO_RESAMPLER_METHOD_KAISER */
            NULL);

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

    // TODO also set max-buffers
    g_object_set(m_gst_data.app_sink, "emit-signals", TRUE, "caps", audio_caps, NULL);
    g_signal_connect(m_gst_data.app_sink, "new-sample", G_CALLBACK(new_sample), &m_gst_data);
    gst_caps_unref(audio_caps);

    gst_bin_add_many(GST_BIN(m_gst_data.pipeline),
            m_gst_data.uridecodebin,
            m_gst_data.audio_convert,
            m_gst_data.audio_resample,
            m_gst_data.caps_filter,
            m_gst_data.app_sink, NULL);

    if (gst_element_link_many(
                m_gst_data.audio_convert,
                m_gst_data.audio_resample,
                m_gst_data.caps_filter,
                m_gst_data.app_sink, NULL) != true) {
        throw runtime_error("Could not link GST elements");
    }

    m_gst_data.bus = gst_element_get_bus(m_gst_data.pipeline);
    gst_bus_add_signal_watch(m_gst_data.bus);
    g_signal_connect(G_OBJECT(m_gst_data.bus), "message::error", (GCallback)error_cb, &m_gst_data);

    gst_element_set_state(m_gst_data.pipeline, GST_STATE_PLAYING);

    m_running = true;
    m_thread = std::thread(&GSTInput::process, this);
}

bool GSTInput::read_source(size_t num_bytes)
{
    return m_running;
}

ICY_TEXT_t GSTInput::get_icy_text() const
{
    ICY_TEXT_t now_playing;
    {
        std::lock_guard<std::mutex> lock(m_nowplaying_mutex);
        now_playing = m_nowplaying;
    }

    return now_playing;
}

void GSTInput::process()
{
    while (m_running) {
        GstMessage *msg = gst_bus_timed_pop(m_gst_data.bus, 100000);

        if (not msg) {
            continue;
        }

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_BUFFERING:
                {
                    gint percent = 0;
                    gst_message_parse_buffering(msg, &percent);
                    //fprintf(stderr, "GST buffering %d\n", percent);
                    break;
                }
            case GST_MESSAGE_TAG:
                {
                    GstTagList *tags = nullptr;
                    gst_message_parse_tag(msg, &tags);
                    //fprintf(stderr, "Got tags from element %s\n", GST_OBJECT_NAME(msg->src));

                    struct user_data_t {
                        string new_artist;
                        string new_title;
                    } user_data;

                    auto extract_title = [](const GstTagList *list, const gchar *tag, void *user_data) {
                        GValue val = { 0, };

                        auto data = (user_data_t*)user_data;

                        gst_tag_list_copy_value(&val, list, tag);

                        if (G_VALUE_HOLDS_STRING(&val)) {
                            if (strcmp(tag, "title") == 0) {
                                data->new_title = g_value_get_string(&val);
                            }
                            else if (strcmp(tag, "artist") == 0) {
                                data->new_artist = g_value_get_string(&val);
                            }
                        }

                        g_value_unset(&val);
                    };

                    gst_tag_list_foreach(tags, extract_title, &user_data);

                    gst_tag_list_unref(tags);

                    {
                        std::lock_guard<std::mutex> lock(m_nowplaying_mutex);
                        if (user_data.new_artist.empty()) {
                            m_nowplaying.useNowPlaying(user_data.new_title);
                        }
                        else {
                            m_nowplaying.useArtistTitle(user_data.new_artist, user_data.new_title);
                        }
                    }
                    break;
                }
            case GST_MESSAGE_ERROR:
                {
                    GError *err = nullptr;
                    gst_message_parse_error(msg, &err, nullptr);
                    fprintf(stderr, "GST error: %s\n", err->message);
                    g_error_free(err);
                    m_fault = true;
                    break;
                }
            case GST_MESSAGE_EOS:
                m_fault = true;
                break;
            default:
                //fprintf(stderr, "GST message %s\n", gst_message_type_get_name(GST_MESSAGE_TYPE(msg)));
                break;
        }
        gst_message_unref(msg);
    }
}

GSTInput::~GSTInput()
{
    m_running = false;

    // Ensures push() doesn't get blocked
    m_gst_data.samplequeue.clear();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_gst_data.bus) {
        gst_object_unref(m_gst_data.bus);
    }

    if (m_gst_data.pipeline) {
        gst_element_set_state(m_gst_data.pipeline, GST_STATE_NULL);
        gst_object_unref(m_gst_data.pipeline);
    }
}

#endif // HAVE_GST
