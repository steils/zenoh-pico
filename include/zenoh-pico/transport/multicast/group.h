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

#ifndef ZENOH_PICO_MULTICAST_GROUP_H
#define ZENOH_PICO_MULTICAST_GROUP_H

#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/transport/peers.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

typedef struct _z_multicast_transport_manager_t _z_multicast_transport_manager_t;
// Multicast group. It listens for incoming multicast messages on specified multicast group.
// The peers managed by this group keep reference to it.
typedef struct _z_multicast_transport_group_t {
    _z_multicast_transport_manager_t *_parent;  // non-owning pointer to the transport manager that owns this group
    _z_multicast_link_t _link;
    // SN numbers - shared between all peers of the same multicast group
    _z_sn_t _sn_tx;
    // Note: we intentionally force the bool fields below to be aligned at the next word boundary.
    // All access to tx-related fields is always done under the transport manager lock, access to other fields is
    // very often done without the lock, since they are only read by background tasks,
    // and we want to avoid possible data races due to tx and non-tx fields being in the same word.
    // This is only required for c99 and specific mcus that do not posses byte-level access to memory
    bool _transmitted;  // indicates if the group has transmitted any message last keep alive check
#if Z_FEATURE_BATCHING == 1
    bool _is_current_tx_frame_reliable;  // indicates if the current frame being built in the batch is reliable
    _z_wbuf_t _tx_buffer;  // tx buffer for batching, otherwise a single shared common buffer is used for all peers
#endif
    _z_zbuf_t _rx_buffer;                 // a common buffer used for incoming messages
    uint16_t _rx_msg_len;                 // length of the last received message, used for streamed links
    _z_listen_listener_id_t _locator_id;  // idx of locator in config._listen array
} _z_multicast_transport_group_t;

typedef _z_t_msg_join_t _z_t_msg_join_t;
z_result_t _z_multicast_transport_group_add_peer(_z_multicast_transport_group_t *group, const _z_link_address_t *addr,
                                                 const _z_t_msg_join_t *msg);
void _z_multicast_transport_group_clear(_z_multicast_transport_group_t *group);
size_t _z_multicast_transport_group_id(const _z_multicast_transport_group_t *group);
uint16_t _z_multicast_transport_group_get_mtu(const _z_multicast_transport_group_t *group);

static inline bool _z_multicast_group_check_peer_compatibility(const _z_multicast_transport_group_t *group,
                                                               const _z_t_msg_join_t *msg) {
    return (msg->_seq_num_res == Z_SN_RESOLUTION) && (msg->_req_id_res == Z_REQ_RESOLUTION) &&
           (msg->_batch_size == _z_multicast_transport_group_get_mtu(group)) && (msg->_version == Z_PROTO_VERSION);
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_MULTICAST_GROUP_H */
