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

#ifndef ZENOH_PICO_UNICAST_PEER_H
#define ZENOH_PICO_UNICAST_PEER_H

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/transport/fragmentation.h"
#include "zenoh-pico/transport/peers.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_UNICAST_TRANSPORT == 1
// Unicast peer. It represents a peer in a unicast connection. It owns a dedicated link.
typedef struct _z_unicast_transport_peer_t {
    _z_unicast_link_t _link;
    _z_id_t _remote_zid;
    _z_zint_t _sn_res;
    _z_sn_t _sn_tx;
    // Note: we intentionally force the bool fields below to be aligned at the next word boundary.
    // All access to tx-related fields is always done under the transport manager lock, access to other fields is
    // very often done without the lock, since they are only read by background tasks,
    // and we want to avoid possible data races due to tx and non-tx fields being in the same word.
    // This is only required for c99 and specific mcus that do not posses byte-level access to memory
    bool _transmitted;  // indicates if the peer has transmitted any message to the remote peer since last keep
                        // alive check
#if Z_FEATURE_BATCHING == 1
    bool _is_current_tx_frame_reliable;  // indicates if the current frame being built in the batch is reliable
    _z_wbuf_t _tx_buffer;  // tx buffer for batching, otherwise a single common buffer is used for all peers
#endif
    _z_zbuf_t _rx_buffer;  // individual buffer is only needed for streamed links, for datagram ones a single
                           // common buffer is used, while this one stays empty and unused
#if Z_FEATURE_FRAGMENTATION == 1
    _z_sn_t _sn_rx;
    _z_dbuf_t _dbuf;
    uint8_t _patch;
#endif
    bool _received;        // indicates if the peer has received any message from the remote peer since last lease check
    uint16_t _rx_msg_len;  // the length of the current message being read (only for streamed links)
    uint8_t _remote_whatami;
    _z_connect_peer_id_t
        _locator_id;  // idx of the locator in the config._connect array that was used to connect to this peer
    uint32_t _lease_duration_ms;  // the lease duration of this peer in milliseconds
    z_clock_t _lease_deadline;    // the time when the lease of this peer will expire
} _z_unicast_transport_peer_t;

// This type is not relocatable.
typedef struct _z_unicast_transport_peer_src_dst_address_t {
    char _src_buf[160];
    char _dst_buf[160];
    _z_string_view_t src;
    _z_string_view_t dst;
} _z_unicast_transport_peer_src_dst_address_t;

void _z_unicast_transport_peer_src_dst_address_get(const _z_unicast_transport_peer_t *peer,
                                                   _z_unicast_transport_peer_src_dst_address_t *address);

void _z_unicast_transport_peer_clear(_z_unicast_transport_peer_t *peer);
static inline void _z_unicast_transport_peer_clear_without_link(_z_unicast_transport_peer_t *peer) {
    peer->_link = _z_unicast_link_null();
    _z_unicast_transport_peer_clear(peer);
}

#endif /* Z_FEATURE_UNICAST_TRANSPORT == 1 */

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_UNICAST_PEER_H */
