/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2012 Clément Bœsch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

 //Based largely on https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/filtering_audio.c

#include "FFMPEGInput.h"

#include "config.h"

#if HAVE_FFMPEG

void FFMPEGInput::prepare() {
    int ret;
    AVCodec *dec;

    avformat_network_init();

    if ((ret = avformat_open_input(&fmt_ctx, m_uri.c_str(), NULL, NULL)) < 0) {
        throw std::runtime_error("Cannot open input");
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        throw std::runtime_error("Cannot find stream information");
    }

    /* select the audio stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (ret < 0) {
        throw std::runtime_error("Cannot find an audio stream in the input file");
    }
    audio_stream_index = ret;

    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        throw std::runtime_error("Could not create decoding context");
    }
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[audio_stream_index]->codecpar);

    /* init the audio decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        throw std::runtime_error("Cannot open audio decoder");
    }
    init_filters();
}

void FFMPEGInput::init_filters() {
    const AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    const enum AVSampleFormat out_sample_fmts[] = { AV_SAMPLE_FMT_S16, (enum AVSampleFormat)-1 };
    const int64_t out_channel_layouts[] = { m_channels == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO, -1 };
    static const int out_sample_rates[] = { m_rate, -1 };
    const AVFilterLink *outlink;
    AVRational time_base = fmt_ctx->streams[audio_stream_index]->time_base;

    int ret;

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        throw std::runtime_error("Could not allocate ffmpeg-filter elements");
    }

    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!dec_ctx->channel_layout)
        dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);

    buffersrc_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersrc, "in");
    if (!buffersrc_ctx) {
        throw std::runtime_error("Cannot alloc audio buffer source");
    }

    char ch_layout[64];
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, dec_ctx->channel_layout);
    ret = av_opt_set(buffersrc_ctx, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
    if (ret>= 0) ret = av_opt_set_q(buffersrc_ctx, "time_base", time_base, AV_OPT_SEARCH_CHILDREN);
    if (ret >= 0) ret = av_opt_set_int(buffersrc_ctx, "sample_rate", dec_ctx->sample_rate, AV_OPT_SEARCH_CHILDREN);
    if (ret >= 0) ret = av_opt_set_sample_fmt(buffersrc_ctx, "sample_fmt", dec_ctx->sample_fmt, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        throw new std::runtime_error("Could not set buffersrc arguments");
    }

    ret = avfilter_init_str(buffersrc_ctx, NULL);
    if (ret < 0) {
        throw std::runtime_error("Cannot create audio buffer source");
    }

    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        throw std::runtime_error("Cannot create audio buffer sink");
    }

    ret = av_opt_set_int_list(buffersink_ctx, "sample_fmts", out_sample_fmts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        throw std::runtime_error("Cannot set output sample format");
    }

    ret = av_opt_set_int_list(buffersink_ctx, "channel_layouts", out_channel_layouts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        throw std::runtime_error("Cannot set output channel layout");
    }

    ret = av_opt_set_int_list(buffersink_ctx, "sample_rates", out_sample_rates, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        throw std::runtime_error("Cannot set output sample rate");
    }


    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, m_filters.c_str(),
                                    &inputs, &outputs, NULL)) < 0) {
        throw std::runtime_error("Could not parse filter graph");
    }

    avfilter_graph_set_auto_convert(filter_graph, AVFILTER_AUTO_CONVERT_ALL);
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0) {
        throw std::runtime_error("Could not configure filter graph");
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
}

bool FFMPEGInput::read_source(size_t num_bytes) {
    int ret;
    AVPacket packet;
    int bytes_send = 0;
    if (m_samplequeue.remaining() < num_bytes) return true;
    while (bytes_send < num_bytes && m_samplequeue.remaining() > 0) {
        if ((ret = av_read_frame(fmt_ctx, &packet)) < 0) {
            break;
        }
        if (packet.stream_index != audio_stream_index) {
            continue;
        }

        ret = avcodec_send_packet(dec_ctx, &packet);
        if (ret < 0) {
            fprintf(stderr,"Error while sending a packet to the decoder\n");
            break;
        }

        while (ret >= 0) {
             ret = avcodec_receive_frame(dec_ctx, frame);
             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                 break;
             }
             else if (ret < 0) {
                 fprintf(stderr, "Error while receiving a frame from the decoder\n");
                 goto end;
             }

             if (ret >= 0) {
                /* push the audio data from decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    fprintf(stderr, "Error while feeding the audio filtergraph\n");
                    break;
                }

                /* pull filtered audio from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0) {
                        fprintf(stderr, "Error while getting audio from the filtergraph\n");
                        goto end;
                    }

                    const uint8_t *p     = reinterpret_cast<uint8_t*>(filt_frame->data[0]);
                    const int n = filt_frame->nb_samples * av_get_channel_layout_nb_channels(filt_frame->channel_layout);

                    m_samplequeue.push(p, n * BYTES_PER_SAMPLE);
                    bytes_send += n * BYTES_PER_SAMPLE;

                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
         }
         av_packet_unref(&packet);
    }
    end:

    if (ret == AVERROR_EOF) {
        return false;
    }

    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        char str_error[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret,str_error, AV_ERROR_MAX_STRING_SIZE );
        fprintf(stderr, "Error! %d %s\n", ret, str_error);
        m_fault = true;
        return false;
    }

    return true;
}

FFMPEGInput::~FFMPEGInput() {
    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
}


#endif
