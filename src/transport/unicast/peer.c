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

#include "zenoh-pico/transport/unicast/peer.h"

#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/protocol/definitions/transport.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1

void _z_unicast_transport_peer_clear(_z_unicast_transport_peer_t *peer) {
    _z_unicast_link_clear(&peer->_link);
#if Z_FEATURE_BATCHING == 1
    _z_wbuf_clear(&peer->_tx_buffer);
#endif
    _z_zbuf_clear(&peer->_rx_buffer);
#if Z_FEATURE_FRAGMENTATION == 1
    _z_dbuf_clear(&peer->_dbuf);
#endif
    _ZP_UNUSED(peer);
}

void _z_unicast_transport_peer_src_dst_address_get(const _z_unicast_transport_peer_t *peer,
                                                   _z_unicast_transport_peer_src_dst_address_t *out) {
    memset(out, 0, sizeof(_z_unicast_transport_peer_src_dst_address_t));
    if (_z_unicast_link_get_locators(&peer->_link, out->_src_buf, _ZP_ARRAY_SIZE(out->_src_buf), out->_dst_buf,
                                     _ZP_ARRAY_SIZE(out->_dst_buf)) == _Z_RES_OK) {
        out->src = _z_string_view_make_from_str(out->_src_buf);
        out->dst = _z_string_view_make_from_str(out->_dst_buf);
    }
}

#endif
