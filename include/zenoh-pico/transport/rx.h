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

#ifndef ZENOH_PICO_TRANSPORT_RX_H
#define ZENOH_PICO_TRANSPORT_RX_H

#include <stdint.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/transport/fragmentation.h"
#include "zenoh-pico/transport/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t _z_transport_read_stream_size(_z_zbuf_t *zbuf);
z_result_t _z_transport_read_message(_z_zbuf_t *rx_buffer, uint16_t *rx_msg_len, _z_transport_message_t *t_msg,
                                     bool *has_more_messages);
z_result_t _z_transport_handle_frame(_z_transport_manager_t *manager, uint8_t header, const _z_t_msg_frame_t *frame,
#if Z_FEATURE_FRAGMENTATION == 1
                                     _z_dbuf_t *dbuf, _z_sn_t *sn_rx,
#endif
                                     size_t peer);

#if Z_FEATURE_FRAGMENTATION == 1
z_result_t _z_transport_handle_fragment(_z_transport_manager_t *manager, uint8_t header, _z_t_msg_fragment_t *msg,
                                        _z_dbuf_t *dbuf, _z_sn_t *sn_rx, _z_zint_t sn_res, uint8_t patch,
                                        size_t peer_iter);
#endif

z_result_t _z_transport_manager_read(_z_transport_manager_t *manager);

#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_NAME _z_ready_links_mask_bitset
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_IS_SET 1
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_SOCKET_READERS
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_BLOCK_TYPE uint8_t
#include "zenoh-pico/collections/static_bit_vector_template.h"

_z_ready_links_mask_bitset_t _z_transport_manager_wait_readable(_z_transport_manager_t *manager);

#if Z_FEATURE_UNICAST_TRANSPORT == 1
static inline bool _z_ready_links_mask_bitset_is_unicast_peer_ready(const _z_ready_links_mask_bitset_t *ready_links,
                                                                    size_t peer_id) {
    return *_z_ready_links_mask_bitset_const_at(ready_links, peer_id);
}
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
static inline bool _z_ready_links_mask_bitset_is_multicast_group_ready(const _z_ready_links_mask_bitset_t *ready_links,
                                                                       size_t group_id) {
    return *_z_ready_links_mask_bitset_const_at(ready_links, group_id + Z_MAX_NUM_UNICAST_PEERS);
}
#endif

#ifdef __cplusplus
}
#endif

#endif  // ZENOH_PICO_TRANSPORT_RX_H
