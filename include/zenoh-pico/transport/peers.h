//
// Copyright (c) 2026 ZettaScale Technology
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//
// Contributors:
//   ZettaScale Zenoh Team, <zenoh@zettascale.tech>
//

#ifndef ZENOH_PICO_TRANSPORT_PEERS_H
#define ZENOH_PICO_TRANSPORT_PEERS_H

#include <stdint.h>

#include "zenoh-pico/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_NAME _z_peer_mask_bitset
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_PEERS
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_BLOCK_TYPE uint8_t
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_IS_SET 1
#include "zenoh-pico/collections/static_bit_vector_template.h"

#define _Z_LOCAL_PEER_ID Z_MAX_NUM_PEERS

#define _ZP_CONNECT_ATTEMPT_MIN_INTERVAL_MS 500
#define _ZP_CONNECT_ATTEMPT_MAX_INTERVAL_MS 2000
#define _ZP_RECONNECT_ATTEMPT_MAX_INTERVAL_MS 64000

#if Z_MAX_NUM_UNICAST_PEERS > 253
typedef uint16_t _z_connect_peer_id_t;
#define _ZP_CONNECT_PEER_ID_FROM_SCOUT ((_z_connect_peer_id_t)0xFFFF)
#define _ZP_CONNECT_PEER_ID_FROM_LISTEN ((_z_connect_peer_id_t)0xFFFE)
#else
typedef uint8_t _z_connect_peer_id_t;
#define _ZP_CONNECT_PEER_ID_FROM_SCOUT ((_z_connect_peer_id_t)0xFF)
#define _ZP_CONNECT_PEER_ID_FROM_LISTEN ((_z_connect_peer_id_t)0xFE)
#endif

#if Z_MAX_NUM_LISTENERS > 255
typedef uint16_t _z_listen_listener_id_t;
#else
typedef uint8_t _z_listen_listener_id_t;
#endif

static inline uint32_t _z_connect_attempt_duration_increase(uint32_t current_duration_ms, uint32_t min_duration_ms,
                                                            uint32_t max_duration_ms) {
    uint32_t new_duration = current_duration_ms * 2;
    if (new_duration > max_duration_ms) {
        new_duration = max_duration_ms;
    } else if (new_duration < min_duration_ms) {
        new_duration = min_duration_ms;
    }
    return new_duration;
}

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
typedef struct _z_connect_info_t {
    _z_peer_mask_bitset_t connected_locators;
    uint32_t next_reconnect_attempt_in_ms;
    size_t next_peer;
} _z_connect_info_t;

static inline void _z_connect_info_init(_z_connect_info_t *info) {
    info->connected_locators = _z_peer_mask_bitset_new();
    info->next_reconnect_attempt_in_ms = 0;
    info->next_peer = 0;
}

static inline void _z_connect_info_increase_duration(_z_connect_info_t *info) {
    info->next_reconnect_attempt_in_ms = _z_connect_attempt_duration_increase(
        info->next_reconnect_attempt_in_ms, _ZP_CONNECT_ATTEMPT_MIN_INTERVAL_MS, _ZP_RECONNECT_ATTEMPT_MAX_INTERVAL_MS);
}

static inline void _z_connect_info_reset_duration(_z_connect_info_t *info) { info->next_reconnect_attempt_in_ms = 0; }

#endif

#ifdef __cplusplus
}
#endif

#endif  // ZENOH_PICO_TRANSPORT_PEERS_H
