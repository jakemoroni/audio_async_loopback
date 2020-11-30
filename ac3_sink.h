/*
 * This file is part of audio_async_loopback
 * Copyright (c) 2020 Jacob Moroni.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _AC3_SINK_H_
#define _AC3_SINK_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <samplerate.h>
#include <pulse/simple.h>
#include <libavcodec/avcodec.h>

#include "config.h"

#define AC3_SINK_NUM_CHANNELS          6

struct ac3_sink {
    pthread_mutex_t lock;
    pthread_t thread;
    pthread_cond_t cond;
    bool thread_run;

    SRC_STATE *rate_converter[AC3_SINK_NUM_CHANNELS];
    pa_simple *pa_inst;

    /* This needs to be large enough to store an entire AC3 frame worth of
     * samples _after_ resampling. The AC3 frames are typically 1536 samples,
     * so add some padding to account for a ratio > 1.
     */
    float tmp_output_buf[AC3_SINK_NUM_CHANNELS][4096];


    float buffer[AC3_SINK_SAMPLE_BUFFER_SIZE];
    uint32_t read_idx;
    uint32_t write_idx;

    SRC_DATA src_data;

    AVCodec *codec;
    AVCodecContext *cctx;
    AVPacket packet;
    AVFrame *frame;

    int32_t history[AC3_SINK_BUFFER_HIST_SIZE];
    uint32_t histidx;
    int32_t average; /* Informational only */
};

void ac3_sink_open(struct ac3_sink *inst, uint32_t latency_us);

void ac3_sink_close(struct ac3_sink *inst);

/* Data is a pointer to a complete AC3 frame. */
void ac3_sink_process(struct ac3_sink *inst, uint8_t *data, size_t len);


#endif /* _AC3_SINK_H_ */
