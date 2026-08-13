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

#include "zenoh-pico/transport/multicast/connectivity.h"

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/session/interest.h"
#include "zenoh-pico/transport/connectivity.h"
#include "zenoh-pico/transport/multicast/group.h"
#include "zenoh-pico/transport/multicast/peer.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/transport.h"

#define _ZP_MAX_ADDRESS_LEN 96

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
#if Z_FEATURE_CONNECTIVITY == 1
static void _z_multicast_transport_manager_report_event(_z_multicast_transport_manager_t *manager,
                                                        _z_address_to_multicast_transport_peer_hmap_iter_t peer_iter,
                                                        bool is_connected) {
    _z_address_to_multicast_transport_peer_hmap_elem_t *entry =
        _z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, peer_iter);
    _z_connectivity_peer_event_data_t peer_data = {0};
    const _z_multicast_transport_group_t *group =
        _z_multicast_transport_group_vec_const_at(&manager->_groups, entry->val._multicast_group);
    uint16_t mtu = (uint16_t)_z_zbuf_capacity(&group->_rx_buffer);
    bool is_streamed = _z_multicast_link_is_streamed(&group->_link);
    bool is_reliable = _z_multicast_link_is_reliable(&group->_link);
    peer_data._remote_zid = entry->val._remote_zid;
    peer_data._remote_whatami = entry->val._remote_whatami;
    _z_multicast_transport_peer_src_dst_address_t address;
    _z_multicast_transport_peer_src_dst_address_get(&entry->val, manager, &entry->key, &address);
    peer_data._link_src = address.src;
    peer_data._link_dst = address.dst;
    peer_data._mcast_group = _z_transport_manager_get_listen_locator(manager->_parent, group->_locator_id);

    if (is_connected) {
        _z_connectivity_peer_connected(manager->_parent->_session, &peer_data, mtu, is_streamed, is_reliable);
    } else {
        _z_connectivity_peer_disconnected(manager->_parent->_session, &peer_data, mtu, is_streamed, is_reliable);
    }
}

#endif
void _z_multicast_transport_manager_report_disconnected_event(
    _z_multicast_transport_manager_t *manager,
    _z_address_to_multicast_transport_peer_hmap_iter_t disconnected_peer_iter) {
    _z_session_t *session = manager->_parent->_session;
    size_t peer_id = (size_t)Z_MAX_NUM_UNICAST_PEERS + (size_t)disconnected_peer_iter;
    _z_flush_remote_resources_for_peer(session, peer_id);
    _z_interest_peer_disconnected(session, peer_id);
#if Z_FEATURE_LIVELINESS == 1 && Z_FEATURE_SUBSCRIPTION == 1
    _z_liveliness_subscription_undeclare_all(session, peer_id);
#endif
#if Z_FEATURE_CONNECTIVITY == 1
    _z_multicast_transport_manager_report_event(manager, disconnected_peer_iter, false);
#endif
}

void _z_multicast_transport_manager_report_connected_event(
    _z_multicast_transport_manager_t *manager, _z_address_to_multicast_transport_peer_hmap_iter_t connected_peer_iter) {
#if Z_FEATURE_CONNECTIVITY == 1
    _z_multicast_transport_manager_report_event(manager, connected_peer_iter, true);
#else
    (void)manager;
    (void)connected_peer_iter;
#endif
}

void _z_multicast_transport_manager_fetch_zid(const _z_multicast_transport_manager_t *manager,
                                              _z_closure_zid_t *callback, z_what_t filter) {
    const _z_multicast_transport_peer_t *peer = NULL;
    _ZP_CONST_FOREACH_VAL (_z_address_to_multicast_transport_peer_hmap, &manager->_peers, peer) {
        if (((z_what_t)peer->_remote_whatami & filter) != 0) {
            (*callback->call)(&peer->_remote_zid, callback->context);
        }
    }
}

bool _z_multicast_transport_manager_has_peer_with_zid(const _z_multicast_transport_manager_t *manager,
                                                      const _z_id_t *zid) {
    const _z_multicast_transport_peer_t *peer = NULL;
    _ZP_CONST_FIND_VAL(_z_address_to_multicast_transport_peer_hmap, &manager->_peers, peer,
                       _z_id_eq(&_->_remote_zid, zid));
    return peer != NULL;
}

#if Z_FEATURE_CONNECTIVITY == 1
void _z_multicast_transport_manager_fetch_transports(const _z_multicast_transport_manager_t *manager,
                                                     z_closure_transport_callback_t call, void *arg) {
    const _z_multicast_transport_peer_t *peer = NULL;
    _ZP_CONST_FOREACH_VAL (_z_address_to_multicast_transport_peer_hmap, &manager->_peers, peer) {
        _z_info_transport_t transport = {0};
        transport._zid = peer->_remote_zid;
        transport._whatami = peer->_remote_whatami;
        transport._is_qos = false;
        transport._is_multicast = true;
        transport._is_shm = false;
        call(&transport, arg);
    }
}

void _z_multicast_transport_manager_fetch_links(const _z_multicast_transport_manager_t *manager,
                                                z_closure_link_callback_t call, void *arg,
                                                const _z_info_transport_t *opt_transport_filter) {
    const _z_address_to_multicast_transport_peer_hmap_elem_t *entry = NULL;
    _ZP_CONST_FOREACH (_z_address_to_multicast_transport_peer_hmap, &manager->_peers, entry) {
        if (opt_transport_filter != NULL &&
            (!opt_transport_filter->_is_multicast || !_z_id_eq(&entry->val._remote_zid, &opt_transport_filter->_zid))) {
            continue;
        }
        const _z_multicast_transport_group_t *group =
            _z_multicast_transport_group_vec_const_at(&manager->_groups, entry->val._multicast_group);
        _z_info_link_t link = {0};
        const _z_multicast_link_t *multicast_link =
            &_z_multicast_transport_group_vec_const_at(&manager->_groups, entry->val._multicast_group)->_link;
        link._is_reliable = _z_multicast_link_is_reliable(multicast_link);
        link._is_streamed = _z_multicast_link_is_streamed(multicast_link);
        link._mtu = _z_multicast_link_get_mtu(multicast_link);
        link._zid = entry->val._remote_zid;
        _z_string_view_t mcast_locator = _z_transport_manager_get_listen_locator(manager->_parent, group->_locator_id);
        link._mcast_group = _z_string_alias(*_z_string_view_deref(&mcast_locator));
        _z_multicast_transport_peer_src_dst_address_t address;
        _z_multicast_transport_peer_src_dst_address_get(&entry->val, manager, &entry->key, &address);
        link._src = _z_string_alias(*_z_string_view_deref(&address.src));
        link._dst = _z_string_alias(*_z_string_view_deref(&address.dst));
        call(&link, arg);
    }
}

#endif

#undef _ZP_MAX_ADDRESS_LEN
#endif  // Z_FEATURE_MULTICAST_TRANSPORT == 1
