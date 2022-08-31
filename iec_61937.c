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
 * IEC 61937 state machine. This processes a stream of samples and
 * extracts the data bursts from an IEC 61937 stream. Once a full
 * burst is acquired, it sends it to the output by calling the
 * callback that was passed during initialization.
 * For now, this only supports AC3 frames because the length field
 * is dependent on the data type. Sometimes it's bits, sometimes it's
 * bytes...
 */

#include <string.h>

#include "iec_61937.h"

/* Burst header sync words. */
#define IEC_61937_SYNC_WORD_0          0xF872
#define IEC_61937_SYNC_WORD_1          0x4E1F
#define IEC_61937_DATA_TYPE_MASK       0x7F

/* Initialize the state machine. */
void iec_61937_fsm_init(struct iec_61937_fsm *inst,
                        iec_61937_packet_cb packet_cb,
                        void *cb_data)
{
    memset(inst, 0, sizeof(struct iec_61937_fsm));

    inst->state = IEC_61937_STATE_FIRST_0;
    inst->packet_cb = packet_cb;
    inst->cb_data = cb_data;

    return;
}

/* Process a single sample. Returns true if locked on to a valid 61937 stream. */
bool iec_61937_fsm_run(struct iec_61937_fsm *inst, uint16_t s16le_sample)
{
    bool ret;
    uint16_t sample;

    ret = false;
    sample = __builtin_bswap16(s16le_sample);

    switch (inst->state) {
    case IEC_61937_STATE_FIRST_0:
        if (sample == 0x0000) {
            inst->state = IEC_61937_STATE_SECOND_0;
        }
        break;
    case IEC_61937_STATE_SECOND_0:
        if (sample == 0x0000) {
            inst->state = IEC_61937_STATE_THIRD_0;;
        } else {
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_THIRD_0:
        if (sample == 0x0000) {
            inst->state = IEC_61937_STATE_FOURTH_0;
        } else {
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_FOURTH_0:
        if (sample == 0x0000) {
            inst->state = IEC_61937_STATE_SYNC_0;
        } else {
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_SYNC_0:
        if (sample == 0x0000) {
            /* Do nothing - might be receiving a stream of 0's. */
        } else if (sample == IEC_61937_SYNC_WORD_0) {
            inst->state = IEC_61937_STATE_SYNC_1;
        } else {
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_SYNC_1:
        if (sample == IEC_61937_SYNC_WORD_1) {
            inst->state = IEC_61937_STATE_DATA_TYPE;
        } else {
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_DATA_TYPE:
        inst->data_type = sample & IEC_61937_DATA_TYPE_MASK;
        inst->state = IEC_61937_STATE_LENGTH;
        if (inst->data_type == IEC_61937_DATA_TYPE_EXTENDED) {
            /* We don't support the extended headers yet. */
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_LENGTH:
        if (inst->data_type == IEC_61937_DATA_TYPE_AC3) {
            inst->bytes_received = 0;
            inst->payload_len = sample / 8u;

            /* NOTE: It's possible for payload len to be odd, but since we
             *       process 16 bit samples at a time, the pad byte just gets
             *       thrown away.
             */
            inst->state = IEC_61937_STATE_PAYLOAD;
        } else {
            /* The length field units depend on the data type. For AC3, it's bits
             * but there's no default, so bail.
             */
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    case IEC_61937_STATE_PAYLOAD:
        if ((inst->payload_len - inst->bytes_received) >= 2u) {
            /* Copy whole sample. */
            inst->payload[inst->bytes_received] = (sample >> 8u);
            inst->bytes_received++;
            inst->payload[inst->bytes_received] = sample;
            inst->bytes_received++;
        } else {
            /* Only need one more byte. */
            inst->payload[inst->bytes_received] = (sample >> 8u);
            inst->bytes_received++;
        }

        if (inst->payload_len == inst->bytes_received) {
            /* Send it. */
            inst->packet_cb(inst->data_type, inst->bytes_received, inst->payload, inst->cb_data);
            inst->state = IEC_61937_STATE_FIRST_0;
        }
        break;
    }

    if (inst->state > IEC_61937_STATE_SYNC_1) {
        /* Declare lock if the second sync word has been received. */
        ret = true;
    }

    return ret;
}
