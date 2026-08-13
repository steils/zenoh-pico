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

#include "zenoh-pico/transport/multicast/peer.h"

#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/transport/connectivity.h"
#include "zenoh-pico/transport/multicast/group.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/transport.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
void _z_multicast_transport_peer_clear(_z_multicast_transport_peer_t *peer) {
#if Z_FEATURE_FRAGMENTATION == 1
    _z_dbuf_clear(&peer->_dbuf);
#endif
    _ZP_UNUSED(peer);
}

void _z_multicast_transport_peer_src_dst_address_get(const _z_multicast_transport_peer_t *peer,
                                                     const _z_multicast_transport_manager_t *manager,
                                                     const _z_link_address_t *peer_address,
                                                     _z_multicast_transport_peer_src_dst_address_t *out) {
    memset(out, 0, sizeof(_z_multicast_transport_peer_src_dst_address_t));
    const _z_multicast_transport_group_t *group =
        _z_multicast_transport_group_vec_const_at(&manager->_groups, peer->_multicast_group);
    out->src = _z_transport_manager_get_listen_locator(manager->_parent, group->_locator_id);
    if (_z_multicast_link_get_dst_locator(&group->_link, _z_link_address_const_data(peer_address),
                                          _z_link_address_size(peer_address), out->_dst_buf,
                                          sizeof(out->_dst_buf)) == _Z_RES_OK) {
        out->dst = _z_string_view_make_from_str(out->_dst_buf);
    }
}
#endif
