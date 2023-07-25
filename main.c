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
 * audio_async_loopback main. Reads from the input and automatically
 * determines whether the incoming audio is PCM or an IEC 61937 bitstream
 * and sends the data to the appropriate sink for decoding and playback.
 * As of now, the only IEC 61937 format supported is 5.1 Channel AC3.
 * Other format support could easily be added later if necessary.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <libavcodec/avcodec.h>

#include "config.h"
#include "iec_61937.h"
#include "pcm_sink.h"
#include "ac3_sink.h"

enum iec_60958_state {
    IEC_60958_STATE_UNKNOWN,
    IEC_60958_STATE_PCM,
    /* Only AC3 is supported for now, with non-AC3 packets getting dropped
     * in the output handler. If more formats are added, the 61937 state
     * should be split into unique states for each format since they will
     * each require different sink implementations.
     */
    IEC_60958_STATE_61937,
};

struct iec_60958 {
    enum iec_60958_state state;
    struct iec_61937_fsm iec_61937_fsm_inst;
    size_t non_61937_chunks;
    struct pcm_sink pcm_sink;
    struct ac3_sink ac3_sink;
    uint32_t sink_latency_us;
};

/* Callback that is called from the IEC 61937 state machine
 * for every data burst received.
 */
static void iec_61937_packet_handler(uint8_t data_type,
                                     size_t len,
                                     uint8_t *payload,
                                     void *handle)
{
    struct iec_60958 *inst = (struct iec_60958 *)handle;

    if (inst->state != IEC_60958_STATE_61937) {
        /* We may still be in the "UNKNOWN" state... */
        return;
    }

    if (data_type != IEC_61937_DATA_TYPE_AC3) {
        /* Discard non-AC3 data. This also discards pause data bursts
         * (if they're actually present in the stream).
         */
        return;
    }

    ac3_sink_process(&inst->ac3_sink, payload, len);
}

/* Passes a chunk to the IEC 61937 state machine and
 * returns true of an IEC 61937 stream was detected
 * within the chunk.
 * NOTE: Chunk size must be a multiple of 2.
 * TODO: Instead of passing around byte arrays, maybe pass around s16 arrays.
 */
static bool process_chunk_iec_61937(struct iec_61937_fsm *inst,
                                    uint8_t *chunk,
                                    size_t chunk_size)
{
    bool ret;
    size_t i;
    uint16_t sample;

    ret = false;

    for (i = 0; i < chunk_size; i += 2) {
        sample = chunk[i];
        sample |= chunk[i + 1] << 8u;

        if (iec_61937_fsm_run(inst, sample)) {
            ret = true;
        }
    }

    return ret;
}

/* Initializes an IEC 60958 context. */
static void iec_60958_init(struct iec_60958 *inst)
{
    memset(inst, 0, sizeof(struct iec_60958));

    inst->state = IEC_60958_STATE_UNKNOWN;
    iec_61937_fsm_init(&inst->iec_61937_fsm_inst, iec_61937_packet_handler, inst);
}

/* Processes a chunk of samples.
 * It is assumed that the array of bytes contains packed
 * 16 bit little endian samples.
 */
static void iec_60958_process(struct iec_60958 *inst,
                              uint8_t *chunk,
                              size_t chunk_size)
{
    switch (inst->state) {
    case IEC_60958_STATE_UNKNOWN:
        if (process_chunk_iec_61937(&inst->iec_61937_fsm_inst, chunk, chunk_size)) {
            /* Found an IEC 61937 stream.
             * NOTE: The call above may have caused some complete data burst
             *       packets to be sent to the callback, but they will have
             *       been dropped since the 61937 sink isn't actually open yet.
             */
            printf("INIT: Found an IEC 61937 stream\n");

            inst->non_61937_chunks = 0;
            inst->state = IEC_60958_STATE_61937;

            ac3_sink_open(&inst->ac3_sink, inst->sink_latency_us);
        } else {
            inst->non_61937_chunks++;
            if (inst->non_61937_chunks >= IEC_61937_DETECTION_WINDOW) {
                printf("INIT: Received %d chunks without a single IEC 61937 data burst; assuming PCM\n",
                       IEC_61937_DETECTION_WINDOW);
                inst->state = IEC_60958_STATE_PCM;

                pcm_sink_open(&inst->pcm_sink, inst->sink_latency_us);
            }
        }
        break;
    case IEC_60958_STATE_PCM:
        /* Always check for IEC 61937 streams even while receiving PCM. */
        if (process_chunk_iec_61937(&inst->iec_61937_fsm_inst, chunk, chunk_size)) {
            /* Going from PCM->61937... */
            printf("Found IEC 61937 stream; switching from PCM\n");

            pcm_sink_close(&inst->pcm_sink);

            inst->non_61937_chunks = 0;
            inst->state = IEC_60958_STATE_61937;

            ac3_sink_open(&inst->ac3_sink, inst->sink_latency_us);
        } else {
            pcm_sink_process(&inst->pcm_sink, chunk);
        }
        break;
    case IEC_60958_STATE_61937:
        if (process_chunk_iec_61937(&inst->iec_61937_fsm_inst, chunk, chunk_size)) {
            /* Got IEC 61937 data so reset counter. */
            inst->non_61937_chunks = 0;
        } else {
            inst->non_61937_chunks++;
            if (inst->non_61937_chunks >= IEC_61937_DETECTION_WINDOW) {
                printf("Received %d chunks without a single IEC 61937 data burst; switching to PCM\n",
                       IEC_61937_DETECTION_WINDOW);
                inst->state = IEC_60958_STATE_PCM;

                ac3_sink_close(&inst->ac3_sink);
                pcm_sink_open(&inst->pcm_sink, inst->sink_latency_us);
            }
        }
        break;
    default:
        printf("Unhandled state %d in 60958 FSM\n", inst->state);
        break;
    }
}

int main(int argc, char*argv[])
{
    int error;
    pa_simple *pa_inst;
    struct iec_60958 iec_60958_inst;
    pa_buffer_attr attr;
    /* Keep the buffer in the BSS. */
    static uint8_t buffer[INPUT_CHUNK_SIZE];

    /* Assume that the S/PDIF interface is always running at a 48 kHz sampling rate */
    static const pa_sample_spec pa_ss = {
        .format = PA_SAMPLE_S16LE,
        .rate = 48000,
        .channels = 2
    };

    static const pa_channel_map channel_map = {
        .channels = 2,
        .map[0] = PA_CHANNEL_POSITION_FRONT_LEFT,
        .map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT,
    };

    if (argc < 2) {
        printf("Usage: audio_async_loopback [input name] [latency microsec]\n");
        printf("       Get input name via: pactl list sources\n");
        printf("       Latency is optional\n");
        return EXIT_FAILURE;
    }

#ifdef FFMPEG_OLD_AUDIO_API
    
    /* Initialize libavcodec. */
    avcodec_register_all();
    
#endif

    /* Configure input buffer for low latency. */
    attr.maxlength = -1;
    attr.tlength = -1;
    attr.prebuf = -1;
    attr.minreq = -1;
    attr.fragsize = INPUT_CHUNK_SIZE;

    /* Open simple pulseaudio context. */
    pa_inst = pa_simple_new(NULL,
                            PROGRAM_NAME_STR,
                            PA_STREAM_RECORD,
                            argv[1],
                            "Audio Async Loopback",
                            &pa_ss,
                            &channel_map,
                            &attr,
                            &error);
    if (!pa_inst) {
        printf("Could not open pulseaudio context (error = %d)\n", error);
        return EXIT_FAILURE;
    }

    /* Open IEC 60958 handler. */
    iec_60958_init(&iec_60958_inst);

    iec_60958_inst.sink_latency_us = 0;
    if (argc == 3) {
        iec_60958_inst.sink_latency_us = atoi(argv[2]);
        if (iec_60958_inst.sink_latency_us == 0) {
            printf("Invalid sink latency, using default\n");
        }
    }

    /* Get sample chunks and process. */
    while (1) {
        if (pa_simple_read(pa_inst, buffer, sizeof(buffer), &error) < 0) {
            printf("Could not read sample chunk (error = %d)\n", error);
            return EXIT_FAILURE;
        }
        iec_60958_process(&iec_60958_inst, buffer, sizeof(buffer));
    }

    return EXIT_SUCCESS;
}
