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

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "Filter.h"

#include "config.h"

using namespace std;

#define SAMPLERATE     48000
#define FORMAT         AV_SAMPLE_FMT_S16
#define CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO

#if 0
static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src,
                             AVFilterContext **sink)
{
    AVFilterGraph *filter_graph;
    AVFilterContext *abuffer_ctx;
    const AVFilter  *abuffer;
    AVFilterContext *volume_ctx;
    const AVFilter  *volume;
    AVFilterContext *aformat_ctx;
    const AVFilter  *aformat;
    AVFilterContext *abuffersink_ctx;
    const AVFilter  *abuffersink;

    AVDictionary *options_dict = NULL;
    char options_str[1024];
    char ch_layout[64];

    int err;

    /* Create a new filtergraph, which will contain all the filters. */
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        fprintf(stderr, "Unable to create filter graph.\n");
        return AVERROR(ENOMEM);
    }

    /* Create the abuffer filter;
     * it will be used for feeding the data into the graph. */
    abuffer = avfilter_get_by_name("abuffer");
    if (!abuffer) {
        fprintf(stderr, "Could not find the abuffer filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    abuffer_ctx = avfilter_graph_alloc_filter(filter_graph, abuffer, "src");
    if (!abuffer_ctx) {
        fprintf(stderr, "Could not allocate the abuffer instance.\n");
        return AVERROR(ENOMEM);
    }

    /* Set the filter options through the AVOptions API. */
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, CHANNEL_LAYOUT);
    av_opt_set    (abuffer_ctx, "channel_layout", ch_layout,                      AV_OPT_SEARCH_CHILDREN);
    av_opt_set    (abuffer_ctx, "sample_fmt",     av_get_sample_fmt_name(FORMAT), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q  (abuffer_ctx, "time_base",      (AVRational){ 1, SAMPLERATE },  AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(abuffer_ctx, "sample_rate",    SAMPLERATE,                     AV_OPT_SEARCH_CHILDREN);

    /* Now initialize the filter; we pass NULL options, since we have already
     * set all the options above. */
    err = avfilter_init_str(abuffer_ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the abuffer filter.\n");
        return err;
    }

    /* Create volume filter. */
    volume = avfilter_get_by_name("volume");
    if (!volume) {
        fprintf(stderr, "Could not find the volume filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    volume_ctx = avfilter_graph_alloc_filter(filter_graph, volume, "volume");
    if (!volume_ctx) {
        fprintf(stderr, "Could not allocate the volume instance.\n");
        return AVERROR(ENOMEM);
    }

    /* A different way of passing the options is as key/value pairs in a
     * dictionary. */
#define VOLUME_VAL 0.90
    av_dict_set(&options_dict, "volume", AV_STRINGIFY(VOLUME_VAL), 0);
    err = avfilter_init_dict(volume_ctx, &options_dict);
    av_dict_free(&options_dict);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the volume filter.\n");
        return err;
    }

    /* Create the aformat filter;
     * it ensures that the output is of the format we want. */
    aformat = avfilter_get_by_name("aformat");
    if (!aformat) {
        fprintf(stderr, "Could not find the aformat filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    aformat_ctx = avfilter_graph_alloc_filter(filter_graph, aformat, "aformat");
    if (!aformat_ctx) {
        fprintf(stderr, "Could not allocate the aformat instance.\n");
        return AVERROR(ENOMEM);
    }

    /* A third way of passing the options is in a string of the form
     * key1=value1:key2=value2.... */
    snprintf(options_str, sizeof(options_str),
             "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%" PRIx64,
             av_get_sample_fmt_name(FORMAT), SAMPLERATE,
             (uint64_t)CHANNEL_LAYOUT);
    err = avfilter_init_str(aformat_ctx, options_str);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not initialize the aformat filter.\n");
        return err;
    }

    /* Finally create the abuffersink filter;
     * it will be used to get the filtered data out of the graph. */
    abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink) {
        fprintf(stderr, "Could not find the abuffersink filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink, "sink");
    if (!abuffersink_ctx) {
        fprintf(stderr, "Could not allocate the abuffersink instance.\n");
        return AVERROR(ENOMEM);
    }

    /* This filter takes no options. */
    err = avfilter_init_str(abuffersink_ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "Could not initialize the abuffersink instance.\n");
        return err;
    }

    /* Connect the filters;
     * in this simple case the filters just form a linear chain. */
    err = avfilter_link(abuffer_ctx, 0, volume_ctx, 0);
    if (err >= 0)
        err = avfilter_link(volume_ctx, 0, aformat_ctx, 0);
    if (err >= 0)
        err = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);
    if (err < 0) {
        fprintf(stderr, "Error connecting filters\n");
        return err;
    }

    /* Configure the graph. */
    err = avfilter_graph_config(filter_graph, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error configuring the filter graph\n");
        return err;
    }

    *graph = filter_graph;
    *src   = abuffer_ctx;
    *sink  = abuffersink_ctx;

    return 0;
}
#endif

int Filter::init_filters(const char *filters_descr)
{
    char args[512];
    char ch_layout[64];
    int ret = 0;
    const AVFilter *abuffer  = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    const AVFilter *aformat = avfilter_get_by_name("aformat");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    const AVFilterLink *outlink;

    m_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !m_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Create the abuffer filter;
     * it will be used for feeding the data into the graph. */
    if (!abuffer) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the abuffer filter.\n");
        ret = AVERROR_FILTER_NOT_FOUND;
        goto end;
    }

    m_src = avfilter_graph_alloc_filter(m_graph, abuffer, "src");
    if (!m_src) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate the abuffer instance.\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Set the filter options through the AVOptions API. */
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, CHANNEL_LAYOUT);
    av_opt_set    (m_src, "channel_layout", ch_layout,                      AV_OPT_SEARCH_CHILDREN);
    av_opt_set    (m_src, "sample_fmt",     av_get_sample_fmt_name(FORMAT), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q  (m_src, "time_base",      (AVRational){ 1, SAMPLERATE },  AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(m_src, "sample_rate",    SAMPLERATE,                     AV_OPT_SEARCH_CHILDREN);

    /* Now initialize the filter; we pass NULL options, since we have already
     * set all the options above. */
    ret = avfilter_init_str(m_src, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not initialize the abuffer filter.\n");
        goto end;
    }

    /* Create the aformat filter;
     * it ensures that the output is of the format we want. */
    if (!aformat) {
        av_log(NULL, AV_LOG_ERROR, "Could not find the aformat filter.\n");
        return AVERROR_FILTER_NOT_FOUND;
    }

    m_aformat = avfilter_graph_alloc_filter(m_graph, aformat, "aformat");
    if (!m_aformat) {
        av_log(NULL, AV_LOG_ERROR, "Could not allocate the aformat instance.\n");
        return AVERROR(ENOMEM);
    }

    /* A third way of passing the options is in a string of the form
     * key1=value1:key2=value2.... */
    snprintf(args, sizeof(args),
             "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%" PRIx64,
             av_get_sample_fmt_name(FORMAT), SAMPLERATE,
             (uint64_t)CHANNEL_LAYOUT);
    ret = avfilter_init_str(m_aformat, args);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not initialize the aformat filter.\n");
        goto end;
    }

    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&m_sink, abuffersink, "out", NULL, NULL, m_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        goto end;
    }

    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, CHANNEL_LAYOUT);
    av_opt_set    (m_sink, "channel_layout", ch_layout,                      AV_OPT_SEARCH_CHILDREN);
    av_opt_set    (m_sink, "sample_fmt",     av_get_sample_fmt_name(FORMAT), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q  (m_sink, "time_base",      (AVRational){ 1, SAMPLERATE },  AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(m_sink, "sample_rate",    SAMPLERATE,                     AV_OPT_SEARCH_CHILDREN);
    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    ret = avfilter_link(m_aformat, 0, m_sink, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot link aformat to sink\n");
        goto end;
    }

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = m_src;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The aformat input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = m_aformat;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(m_graph, filters_descr, &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(m_graph, NULL)) < 0)
        goto end;

    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = m_sink->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
           (int)outlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name((AVSampleFormat)outlink->format), "?"),
           args);

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

Filter::Filter(const std::string& filter_description)
{
    /* Allocate the frame we will be using to store the data. */
    m_frame = av_frame_alloc();
    if (!m_frame) {
        throw runtime_error("Error allocating filter frame");
    }

#if 0
    int err = init_filter_graph(&m_graph, &m_src, &m_sink);
#else
    int err = init_filters(filter_description.c_str());
#endif

    if (err < 0) {
        char errstr[1024];
        av_strerror(err, errstr, sizeof(errstr));
        throw runtime_error(string{"Unable to init filter graph: "} + errstr);
    }
}

Filter::~Filter()
{
    avfilter_graph_free(&m_graph);
    av_frame_free(&m_frame);
}

std::vector<uint8_t> Filter::filter(const std::vector<uint8_t>& data)
{
    if (data.empty()) {
        return data;
    }
    char errstr[1024];

    constexpr size_t bytes_per_sample = 4;

    m_frame->sample_rate    = SAMPLERATE;
    m_frame->format         = FORMAT;
    m_frame->channel_layout = CHANNEL_LAYOUT;
    m_frame->nb_samples     = data.size() / bytes_per_sample; // channels, bytes_per_sample
    m_frame->pts            = m_num_samples_seen;

    int err = av_frame_get_buffer(m_frame, 0);
    if (err < 0) {
        av_strerror(err, errstr, sizeof(errstr));
        throw runtime_error(string{"Unable to get filter frame buffer: "} + errstr);
    }

    memcpy(m_frame->data[0], data.data(), data.size());
    m_num_samples_seen += m_frame->nb_samples;

    /* Send the frame to the input of the filtergraph. */
    err = av_buffersrc_add_frame(m_src, m_frame);
    if (err < 0) {
        av_frame_unref(m_frame);

        av_strerror(err, errstr, sizeof(errstr));
        throw runtime_error(string{"Error submitting the frame to the filtergraph:"} + errstr);
    }

    std::vector<uint8_t> outbuf;
    outbuf.reserve(data.size());

    while ((err = av_buffersink_get_frame(m_sink, m_frame)) >= 0) {
        copy(m_frame->data[0], m_frame->data[0] + m_frame->nb_samples * bytes_per_sample,
                back_inserter(outbuf));
        av_frame_unref(m_frame);
    }

    return outbuf;
}

#if 0
/* Do something useful with the filtered data: this simple
 * example just prints the MD5 checksum of each plane to stdout. */
static int process_output(struct AVMD5 *md5, AVFrame *frame)
{
    int planar     = av_sample_fmt_is_planar(frame->format);
    int channels   = av_get_channel_layout_nb_channels(frame->channel_layout);
    int planes     = planar ? channels : 1;
    int bps        = av_get_bytes_per_sample(frame->format);
    int plane_size = bps * frame->nb_samples * (planar ? 1 : channels);
    int i, j;

    for (i = 0; i < planes; i++) {
        uint8_t checksum[16];

        av_md5_init(md5);
        av_md5_sum(checksum, frame->extended_data[i], plane_size);

        fprintf(stdout, "plane %d: 0x", i);
        for (j = 0; j < sizeof(checksum); j++)
            fprintf(stdout, "%02X", checksum[j]);
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "\n");

    return 0;
}

/* Construct a frame of audio data to be filtered;
 * this simple example just synthesizes a sine wave. */
static int get_input(AVFrame *frame, int frame_num)
{
    int err, i, j;

#define FRAME_SIZE 1024

    /* Set up the frame properties and allocate the buffer for the data. */
    frame->sample_rate    = INPUT_SAMPLERATE;
    frame->format         = INPUT_FORMAT;
    frame->channel_layout = INPUT_CHANNEL_LAYOUT;
    frame->nb_samples     = FRAME_SIZE;
    frame->pts            = frame_num * FRAME_SIZE;

    err = av_frame_get_buffer(frame, 0);
    if (err < 0)
        return err;

    /* Fill the data for each channel. */
    for (i = 0; i < 5; i++) {
        float *data = (float*)frame->extended_data[i];

        for (j = 0; j < frame->nb_samples; j++)
            data[j] = sin(2 * M_PI * (frame_num + j) * (i + 1) / FRAME_SIZE);
    }

    return 0;
}

#endif
