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

#ifndef _IEC_61937_H_
#define _IEC_61937_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Max burst payload length (assuming the length is in bytes). */
#define IEC_61937_MAX_BURST_PAYLOAD    0x10000

enum iec_61937_data_type {
    IEC_61937_DATA_TYPE_AC3      = 0x01,
    IEC_61937_DATA_TYPE_EXTENDED = 0x1F,
};

enum iec_61937_state {
    /* IEC 61937 states that there should always be four 16 bit samples
     * before every burst header. This is supposed to make it easier to
     * identify IEC 61937 streams by effectively increasing the sync word
     * to 96 bits.
     */
    IEC_61937_STATE_FIRST_0,
    IEC_61937_STATE_SECOND_0,
    IEC_61937_STATE_THIRD_0,
    IEC_61937_STATE_FOURTH_0,
    IEC_61937_STATE_SYNC_0,
    IEC_61937_STATE_SYNC_1,
    IEC_61937_STATE_DATA_TYPE,
    IEC_61937_STATE_LENGTH,
    IEC_61937_STATE_PAYLOAD,
};

typedef void (*iec_61937_packet_cb)(uint8_t data_type,
                                    size_t len,
                                    uint8_t *payload,
                                    void *handle);

struct iec_61937_fsm {
    enum iec_61937_state state;
    iec_61937_packet_cb packet_cb;
    void *cb_data;
    uint8_t data_type;
    size_t payload_len;
    size_t bytes_received;
    uint8_t payload[IEC_61937_MAX_BURST_PAYLOAD];
};

void iec_61937_fsm_init(struct iec_61937_fsm *inst,
                        iec_61937_packet_cb packet_cb,
                        void *cb_data);

bool iec_61937_fsm_run(struct iec_61937_fsm *inst, uint16_t s16le_sample);


#endif /* _IEC_61937_H_ */
