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

#include "zenoh-pico/transport/multicast/group.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/transport/connectivity.h"
#include "zenoh-pico/transport/multicast/connectivity.h"
#include "zenoh-pico/transport/multicast/peer.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/multicast/tx.h"
#include "zenoh-pico/transport/transport.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
z_result_t _z_multicast_transport_group_add_peer(_z_multicast_transport_group_t *group, const _z_link_address_t *addr,
                                                 const _z_t_msg_join_t *msg) {
    if (_z_id_eq(&group->_parent->_parent->_session->_local_zid, &msg->_zid)) {
        return _Z_ATTEMPT_SELF_CONNECT;
    }
    if (_z_transport_manager_has_peer_with_zid(group->_parent->_parent, &msg->_zid)) {
        return _Z_ALREADY_CONNECTED;
    }

    if (!_z_multicast_group_check_peer_compatibility(group, msg)) {
        _Z_INFO("Couldn't accept peer on mcast group %zu because distant node is incompatible config wise.",
                _z_multicast_transport_group_id(group));
        _Z_ERROR_RETURN(_Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION);
    }
    // Initialize entry
    _z_multicast_transport_peer_t peer;
    memset(&peer, 0, sizeof(_z_multicast_transport_peer_t));
    peer._lease_duration_ms = (uint32_t)msg->_lease;
    peer._lease_deadline = z_clock_now();
    z_clock_advance_ms(&peer._lease_deadline, (unsigned long)peer._lease_duration_ms);
    peer._remote_zid = msg->_zid;
    peer._remote_whatami = msg->_whatami;
    peer._received = true;
    peer._multicast_group = (uint8_t)(group - _z_multicast_transport_group_vec_const_data(&group->_parent->_groups));
#if Z_FEATURE_FRAGMENTATION == 1
    peer._sn_rx = msg->_next_sn;
    _z_sn_decrement_all(&peer._sn_rx, group->_parent->_sn_res);
    peer._patch = msg->_patch < _Z_CURRENT_PATCH ? msg->_patch : _Z_CURRENT_PATCH;
    _Z_RETURN_IF_ERR(_z_dbuf_init(&peer._dbuf));
#endif

    _Z_CLEAN_RETURN_IF_ERR(_z_transport_manager_lock(group->_parent->_parent),
                           _z_multicast_transport_peer_clear(&peer));
    _z_address_to_multicast_transport_peer_hmap_t *peers = &group->_parent->_peers;
    _z_address_to_multicast_transport_peer_hmap_iter_t iter =
        _z_address_to_multicast_transport_peer_hmap_insert(peers, addr, &peer);
    _z_transport_manager_unlock(group->_parent->_parent);
    if (iter == _z_address_to_multicast_transport_peer_hmap_end(peers)) {
        _z_multicast_transport_peer_clear(&peer);
        return _Z_CAPACITY_LIMIT_REACHED;
    }
    _Z_INFO("Added new peer " _Z_ID_PRINT_FORMAT " on multicast group: %zu", _Z_ID_PRINT_ARGS(&peer._remote_zid),
            _z_multicast_transport_group_id(group));
    _z_multicast_lease_pqueue_push(&group->_parent->_lease_pqueue, &iter);
    _z_transport_manager_signal_opened_peer(group->_parent->_parent, _ZP_CONNECT_PEER_ID_FROM_LISTEN);
    _z_multicast_transport_manager_report_connected_event(group->_parent, iter);
    return _Z_RES_OK;
}

void _z_multicast_transport_group_clear(_z_multicast_transport_group_t *group) {
    size_t group_id = (size_t)(group - _z_multicast_transport_group_vec_const_data(&group->_parent->_groups));
    _ZP_REMOVE_ALL(_z_address_to_multicast_transport_peer_hmap, &group->_parent->_peers,
                   _->val._multicast_group == (uint8_t)group_id);
    _z_multicast_link_clear(&group->_link);
#if Z_FEATURE_BATCHING == 1
    _z_wbuf_clear(&group->_tx_buffer);
#endif
    _z_zbuf_clear(&group->_rx_buffer);
}

size_t _z_multicast_transport_group_id(const _z_multicast_transport_group_t *group) {
    return (size_t)(group - _z_multicast_transport_group_vec_const_data(&group->_parent->_groups));
}

uint16_t _z_multicast_transport_group_get_mtu(const _z_multicast_transport_group_t *group) {
    uint16_t mtu = _z_multicast_link_get_mtu(&group->_link);
    return mtu < Z_BATCH_MULTICAST_SIZE ? mtu : Z_BATCH_MULTICAST_SIZE;
}
#endif
