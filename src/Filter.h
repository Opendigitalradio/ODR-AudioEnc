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
/*! \file Filter.h
 *
 */

#pragma once

#include "config.h"
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

class Filter {
    public:
        Filter(const std::string& filter_description);
        Filter(const Filter&) = delete;
        Filter& operator=(const Filter&) = delete;
        ~Filter();

        std::vector<uint8_t> filter(const std::vector<uint8_t>& data);

    private:
        int init_filters(const char *filters_descr);

        AVFilterGraph *m_graph = nullptr;
        AVFilterContext *m_src = nullptr;
        AVFilterContext *m_sink = nullptr;
        AVFilterContext *m_aformat = nullptr;
        AVFrame *m_frame = nullptr;

        size_t m_num_samples_seen = 0;
};

