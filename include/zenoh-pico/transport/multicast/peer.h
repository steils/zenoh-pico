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

#ifndef ZENOH_PICO_MULTICAST_PEER_H
#define ZENOH_PICO_MULTICAST_PEER_H

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/transport/fragmentation.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
// Multicast peer. It represents a peer in a multicast group. It does not own a dedicated link,
// but it is managed by a multicast listener that owns the link and listens for incoming messages on the multicast
// group.
typedef struct _z_multicast_transport_peer_t {
    _z_id_t _remote_zid;
    z_whatami_t _remote_whatami;
    uint32_t _lease_duration_ms;
    z_clock_t _lease_deadline;
#if Z_FEATURE_FRAGMENTATION == 1
    _z_dbuf_t _dbuf;
    _z_sn_t _sn_rx;
    uint8_t _patch;
#endif
    uint8_t _multicast_group;  // multicast group number, corresponds to id of multicast listener that manages this peer
    bool _received;  // indicates if the peer has received any message from the remote peer since last lease check
} _z_multicast_transport_peer_t;

// This type is not relocatable.
typedef struct _z_multicast_transport_peer_src_dst_address_t {
    char _dst_buf[160];
    _z_string_view_t src;
    _z_string_view_t dst;
} _z_multicast_transport_peer_src_dst_address_t;

typedef struct _z_multicast_transport_manager_t _z_multicast_transport_manager_t;
void _z_multicast_transport_peer_src_dst_address_get(const _z_multicast_transport_peer_t *peer,
                                                     const _z_multicast_transport_manager_t *manager,
                                                     const _z_link_address_t *peer_address,
                                                     _z_multicast_transport_peer_src_dst_address_t *out);

void _z_multicast_transport_peer_clear(_z_multicast_transport_peer_t *peer);

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_MULTICAST_PEER_H */
