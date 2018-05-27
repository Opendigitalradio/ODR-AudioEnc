#pragma once

#include "config.h"

#if HAVE_FFMPEG

#include "SampleQueue.h"
#include "common.h"
#include "InputInterface.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
}

class FFMPEGInput : public InputInterface
{
    public:
        FFMPEGInput(const std::string& uri,
                const std::string& filters,
                int rate,
                unsigned channels,
                SampleQueue<uint8_t>& queue) :
            m_uri(uri),
            m_filters(filters),
            m_channels(channels),
            m_rate(rate),
            m_fault(false),
            m_samplequeue(queue) {
                frame = av_frame_alloc();
                filt_frame = av_frame_alloc();
        }
        ~FFMPEGInput() override;

        virtual void prepare() override;
        virtual bool fault_detected(void) const override { return m_fault; };
        virtual bool read_source(size_t num_bytes) override;


    private:
        std::string m_uri;
        std::string m_filters;
        int m_rate;
        bool m_fault;
        unsigned m_channels;
        SampleQueue<uint8_t>& m_samplequeue;

        //FFMPEG Data structures:
        AVFormatContext *fmt_ctx;
        AVCodecContext *dec_ctx;
        int audio_stream_index = -1;
        AVFrame *frame;
        AVFrame *filt_frame;
        AVFilterContext *buffersink_ctx;
        AVFilterContext *buffersrc_ctx;
        AVFilterGraph *filter_graph;

        void init_filters();

};

#endif
