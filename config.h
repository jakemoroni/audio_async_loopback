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

#ifndef _CONFIG_H_
#define _CONFIG_H_

#define PROGRAM_NAME_STR               "audio_async_loopback"
//#define DEBUG                          1

/* When Pulseaudio is configured for 4 channels (surround 4.0), the
 * sink ends up with Front Left, Front Right, Rear Left, Rear Right.
 * However, AC3 calls the rear channels "side". If the AC3 mapping
 * is used, no audio will come from the rear channels.
 * So, when this is undefined, the AC3 "side" channels get mapped
 * to the sink "rear" channels.
 * I don't think this should ever be defined, but it's here just
 * in case.
 */
#undef USE_AC3_SURROUND_MAPPING

/* Input data is read out in chunks of this size (in bytes). */
#define INPUT_CHUNK_SIZE               512u /* 2.6 millisecond chunks */

/* Number of input chunks that must pass without receiving a single
 * IEC 61937 data burst before considering the input to be a PCM
 * stream.
 */
#define IEC_61937_DETECTION_WINDOW     64u

/* Proportional gain for PCM sink buffer utilization
 * control loop. The max sampling rate ratio is:
 * (PCM_SINK_BUFFER_TARGET_SAMPLES * PCM_SINK_LOOP_GAIN) + 1.
 * This is kept low enough to limit the max ratio to something
 * that won't result in audible pitch changes.
 */
#define PCM_SINK_LOOP_GAIN             0.000004

/* Number of samples to aim to keep in the buffer right before
 * the next chunk is added (so, the minimim utilization).
 * This should be as low as possible while still being able to
 * get a reasonable measurement. If it's too low, then jitter
 * may cause larger buffer depletions to not be accounted for.
 * In other words, if your jitter is +- 64 samples, then this
 * should at least be 128.
 */
#define PCM_SINK_BUFFER_TARGET_SAMPLES     128

/* PCM sink ring buffer size. Large enough to handle some
 * backups, but not so large that it takes forever to each
 * the target utilization level.
 */
#define PCM_SINK_SAMPLE_BUFFER_SIZE        2048u
#define PCM_SINK_SAMPLE_BUFFER_SIZE_MASK   (PCM_SINK_SAMPLE_BUFFER_SIZE - 1u)

/* Minimum amount of samples that we will attempt to write
 * to the PCM sink output stream. Note that this is SAMPLES
 * and not L/R frames or bytes.
 * NOTE: This must be a multiple of 2 otherwise the Pulseaudio
 *       sink can become out of sync w.r.t left/right.
 */
#define PCM_SINK_OUTPUT_CHUNK_SIZE     32u

/* PCM sink Pulseaudio output buffer. This directly impacts
 * the overall latency, so keep it small, but not so small
 * that there are a lot of underruns.
 * Even though the sampling rate is being adjusted dynamically
 * to maintain a buffer level, systems with high scheduling
 * jitter can still underrun/overflow the buffer.
 */
#define PCM_SINK_PA_BUFFER_SIZE        2048u /* 5.3 milliseconds */

/* See comment above about the PCM sink. Note that this is
 * 6 times larger than the PCM value divided by two.
 * For the PCM sink we write 16 samples per channel, so 32 total,
 * so here we write 96.
 */
#define AC3_SINK_OUTPUT_CHUNK_SIZE     96u

/* Same as PCM sink, but adjusted for 6 channels. */
#define AC3_SINK_PA_BUFFER_SIZE        6144u


/* This is just like the PCM sink, but scaled to compensate
 * for 6 channels worth of samples.
 */
#define AC3_SINK_LOOP_GAIN             0.0000013334

/* See PCM comments above. */
#define AC3_BUFFER_TARGET_SAMPLES      384

/* See PCM comments above. */
#define AC3_SINK_SAMPLE_BUFFER_SIZE        32768u
#define AC3_SINK_SAMPLE_BUFFER_SIZE_MASK   (AC3_SINK_SAMPLE_BUFFER_SIZE - 1u)


#endif /* _CONFIG_H_ */
