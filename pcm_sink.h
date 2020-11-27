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

#ifndef _PCM_SINK_H_
#define _PCM_SINK_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <samplerate.h>
#include <pulse/simple.h>

#include "config.h"

struct pcm_sink {
    pthread_mutex_t lock;
    pthread_t thread;
    pthread_cond_t cond;
    bool thread_run;

    SRC_STATE *rate_converter;
    pa_simple *pa_inst;

    /* The input buffer is basically a chunk but converted from
     * int16_t to float. So, chunk size is 128 bytes, which is 64 samples,
     * so we need 64 floats.
     */
    float tmp_input_buf[INPUT_CHUNK_SIZE / 2];

    /* The output can actually be larger than the input. For example,
     * if the ratio is >2. Our ratio is limited to like 1.1, but let's
     * just use double the buffer. This would leave room for something like
     * 48k in and 96k out.
     */
    float tmp_output_buf[INPUT_CHUNK_SIZE];

    float buffer[PCM_SINK_SAMPLE_BUFFER_SIZE];
    uint32_t read_idx;
    uint32_t write_idx;

    SRC_DATA src_data;
};

void pcm_sink_open(struct pcm_sink *inst);

void pcm_sink_close(struct pcm_sink *inst);

/* Data is a pointer to interleaved left/right 16 bit samples. */
void pcm_sink_process(struct pcm_sink *inst, uint8_t *data);


#endif /* _PCM_SINK_H_ */
