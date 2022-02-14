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

/*
 * Main PCM sink implementation. Accepts an array of interleaved
 * left/right s16le samples, converts them to float, passes them
 * through the resampler, the finally to the Pulseaudio output.
 * The sampling rate ratio is dynamically adjusted to attempt to
 * maintain a constant amount of data in the intermediate buffer.
 * This is intended to compensate for the fact that the data may
 * be coming in and leaving from two different clock domains, like
 * if you're capturing from a USB interface and playing back via
 * a PCI soundcard.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pcm_sink.h"
#include "config.h"

/* Returns the number of space available, in samples. */
static uint32_t buffer_space_avail(struct pcm_sink *inst) {
    return (PCM_SINK_SAMPLE_BUFFER_SIZE_MASK -
            ((inst->write_idx - inst->read_idx) & PCM_SINK_SAMPLE_BUFFER_SIZE_MASK));
}

/* Returns the current buffer utilization, in samples. */
static uint32_t buffer_used(struct pcm_sink *inst) {
    return ((inst->write_idx - inst->read_idx) & PCM_SINK_SAMPLE_BUFFER_SIZE_MASK);
}

/* Output thread. Writes data from the intermediate buffer into
 * the Pulseaudio stream in units of PCM_SINK_OUTPUT_CHUNK_SIZE
 * samples.
 */
static void *output_thread(void *arg) {
    int error;
    uint32_t i;
    float tmp[PCM_SINK_OUTPUT_CHUNK_SIZE];
    struct pcm_sink *inst = (struct pcm_sink *) arg;

    while (1) {
        pthread_mutex_lock(&inst->lock);

        /* Wait for data. */
        while ((buffer_used(inst) < PCM_SINK_OUTPUT_CHUNK_SIZE) && inst->thread_run) {
            pthread_cond_wait(&inst->cond, &inst->lock);
        }

        if (!inst->thread_run) {
            /* Terminate. */
            pthread_mutex_unlock(&inst->lock);
            pthread_exit(NULL);
        }

        /* Copy out one chunk. */
        for (i = 0; i < PCM_SINK_OUTPUT_CHUNK_SIZE; i++) {
            tmp[i] = inst->buffer[inst->read_idx];
            inst->read_idx++;
            inst->read_idx &= PCM_SINK_SAMPLE_BUFFER_SIZE_MASK;
        }

        pthread_mutex_unlock(&inst->lock);

        if (pa_simple_write(inst->pa_inst, tmp, sizeof(tmp), &error) < 0) {
            printf("Could not write chunk to output stream (error = %d)\n", error);
        }
    }

    /* Not reached. */
    pthread_exit(NULL);
}

/* Calculate a new sampling rate ratio. This should be called
 * before adding a new chunk to the ring buffer, and must be
 * called with the lock held.
 */
static double calculate_rate_ratio(struct pcm_sink *inst) {
    size_t i;
    double accum;
    const int32_t tmp = buffer_used(inst);
    const double mult = PCM_SINK_LOOP_GAIN;
    int32_t offset = PCM_SINK_BUFFER_TARGET_SAMPLES - tmp;

    /* Clamp the max offset so that the max rate ratio is
     * purely limited by the gain.
     */
    if (offset < -PCM_SINK_BUFFER_TARGET_SAMPLES) {
        offset = -PCM_SINK_BUFFER_TARGET_SAMPLES;
    } else if (offset > PCM_SINK_BUFFER_TARGET_SAMPLES) {
        offset = PCM_SINK_BUFFER_TARGET_SAMPLES;
    }

    inst->history[inst->histidx] = offset;
    inst->histidx++;
    inst->histidx &= (PCM_SINK_BUFFER_HIST_SIZE - 1u);

    accum = 0;
    for (i = 0; i < PCM_SINK_BUFFER_HIST_SIZE; i++) {
        accum += inst->history[i];
    }
    accum /= PCM_SINK_BUFFER_HIST_SIZE;

    inst->average = accum;

    return ((mult * accum) + 1.0);
}

/* Get the Pulseaudio buffer size required to achieve the
 * requested latency.
 */
static uint32_t calculate_pa_buf_size(struct pcm_sink *inst,
                                      uint32_t latency_us) {
    const double latency_seconds = ((double) latency_us / 1000000.0);
    const double latency_samples = latency_seconds / (1.0 / 48000.0);
    /* Two channels, 4 byte samples. */
    const uint32_t bytes = latency_samples * 4u * 2u;

    if (!latency_us || (bytes < PCM_SINK_PA_BUFFER_SIZE)) {
        printf("Using default sink buffer size of %d bytes\n", PCM_SINK_PA_BUFFER_SIZE);
        return PCM_SINK_PA_BUFFER_SIZE;
    }

    printf("PA buffer size = %d bytes\n", bytes);

    return bytes;
}

/* Open the PCM sink. */
void pcm_sink_open(struct pcm_sink *inst, uint32_t latency_us) {
    int error;
    uint32_t bufsize;
    pa_buffer_attr attr;

    static const pa_sample_spec pa_ss = {
            .format = PA_SAMPLE_FLOAT32LE,
            .rate = 48000,
            .channels = 2
    };

    memset(inst, 0, sizeof(struct pcm_sink));

    /* Initialize buffer to be at the target. This provides a better starting point for the loop. */
    inst->write_idx = PCM_SINK_BUFFER_TARGET_SAMPLES;

    pthread_mutex_init(&inst->lock, NULL);
    pthread_cond_init(&inst->cond, NULL);

    inst->rate_converter = src_new(SRC_SINC_BEST_QUALITY, 2, &error);
    if (!inst->rate_converter) {
        printf("Could not create sample rate converter instance\n");
        /* TODO - Handle failure. Program will crash if output is called... */
    }

    /* Configure buffer for low latency. */
    bufsize = calculate_pa_buf_size(inst, latency_us);
    attr.maxlength = bufsize;
    attr.tlength = bufsize;
    attr.prebuf = bufsize;
    attr.minreq = 8;
    attr.fragsize = -1;

    pa_channel_map channel_map;

    /* Open simple pulseaudio context. */
    inst->pa_inst = pa_simple_new(NULL,
                                  PROGRAM_NAME_STR,
                                  PA_STREAM_PLAYBACK,
                                  NULL,
                                  "Audio Async Loopback",
                                  &pa_ss,
#if PCM_PREFER_STEREO
                                  pa_channel_map_init_stereo(&channel_map),
#else
                                  NULL,
#endif
                                  &attr,
                                  &error);
    if (!inst->pa_inst) {
        printf("Could not open Pulseaudio context (error = %d)\n", error);
        /* TODO - Handle failure. Program will crash if output is called... */
    }

    /* Pre-set these fields as an optimization. Only the required
     * fields get updated in the process call.
     */
    inst->src_data.data_in = inst->tmp_input_buf;
    inst->src_data.data_out = inst->tmp_output_buf;
    /* One frame == one left right sample pair. */
    inst->src_data.input_frames = (sizeof(inst->tmp_input_buf) / sizeof(float)) / 2u;
    inst->src_data.output_frames = (sizeof(inst->tmp_output_buf) / sizeof(float)) / 2u;
    inst->src_data.end_of_input = 0;
    inst->src_data.src_ratio = 1.0;

    inst->thread_run = true;
    pthread_create(&inst->thread, NULL, output_thread, inst);
    /* TODO - Check return. */
}

/* Close the PCM sink. */
void pcm_sink_close(struct pcm_sink *inst) {
    int error;

    /* Kill the thread. */
    pthread_mutex_lock(&inst->lock);
    inst->thread_run = false;
    pthread_cond_broadcast(&inst->cond);
    pthread_mutex_unlock(&inst->lock);
    pthread_join(inst->thread, NULL);

    /* Kill Pulseaudio connection. */
    pa_simple_flush(inst->pa_inst, &error);
    pa_simple_free(inst->pa_inst);

    /* Cleanup the rate converter. */
    src_delete(inst->rate_converter);
}

/* Send a chunk of interleaved left/right s16le PCM samples
 * to the sink. There's no length argument because this sub-module
 * relies on the top level chunk size anyway...
 */
void pcm_sink_process(struct pcm_sink *inst, uint8_t *data) {
    int error;
    uint32_t can_queue;
    uint32_t will_queue;
    uint32_t i;
    const uint32_t nr_samples = INPUT_CHUNK_SIZE / 2u;

    /* We should be getting left/right pairs... */
    if (nr_samples & 0x1) {
        printf("Program error - odd number of samples\n");
        exit(1);
    }

    /* First, run the data through the resampler. All input
     * data must pass through the resampler even if it ends
     * up getting dropped.
     */

    /* Convert array of int16le to float. */
    for (i = 0; i < nr_samples; i++) {
        uint16_t tmp;
        int16_t s16le_sample;

        tmp = data[(i * 2u) + 1u];
        tmp <<= 8u;
        tmp |= data[i * 2u];

        s16le_sample = tmp;

        /* Same conversion used by Pulseaudio. */
        inst->tmp_input_buf[i] = s16le_sample * (1.0f / (1u << 15u));
    }

    /* Resample. */
    if ((error = src_process(inst->rate_converter, &inst->src_data))) {
        printf("PCM sink rate converter error %s\n", src_strerror(error));
    }

    pthread_mutex_lock(&inst->lock);

    inst->src_data.src_ratio = calculate_rate_ratio(inst);

#ifdef DEBUG
    printf("Buffer: %04d    Ratio: %f    Avg: %d\n", buffer_used(inst), inst->src_data.src_ratio, inst->average);
#endif

    /* First, figure out how many samples we can queue.
     * NOTE: This relies on the fact that the data is drained
     *       in pairs of samples. If for example only one sample
     *       is drained, then our Pulseaudio sink might get out
     *       of sync w.r.t left/right.
     */
    can_queue = buffer_space_avail(inst);

    /* MIN */
    if (can_queue < (inst->src_data.output_frames_gen * 2u)) {
        will_queue = can_queue;
    } else {
        will_queue = (inst->src_data.output_frames_gen * 2u);
    }

    for (i = 0; i < will_queue; i++) {
        inst->buffer[inst->write_idx] = inst->tmp_output_buf[i];
        inst->write_idx++;
        inst->write_idx &= PCM_SINK_SAMPLE_BUFFER_SIZE_MASK;
    }

    pthread_mutex_unlock(&inst->lock);
    pthread_cond_broadcast(&inst->cond);
}
