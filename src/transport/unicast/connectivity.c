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
#include "zenoh-pico/transport/unicast/connectivity.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/session/interest.h"
#include "zenoh-pico/transport/unicast/transport.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1
#if Z_FEATURE_CONNECTIVITY == 1
static void _z_unicast_transport_manager_report_event(_z_unicast_transport_manager_t *manager,
                                                      _z_address_to_unicast_transport_peer_hmap_iter_t peer_iter,
                                                      bool is_connected) {
    _z_connectivity_peer_event_data_t peer_event_data = {0};
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, peer_iter)->val;
    uint16_t mtu = _z_unicast_link_get_mtu(&peer->_link);
    bool is_streamed = _z_unicast_link_is_streamed(&peer->_link);
    bool is_reliable = _z_unicast_link_is_reliable(&peer->_link);

    peer_event_data._remote_zid = peer->_remote_zid;
    peer_event_data._remote_whatami = (z_whatami_t)peer->_remote_whatami;
    _z_unicast_transport_peer_src_dst_address_t address;
    _z_unicast_transport_peer_src_dst_address_get(&peer->_link, &address);
    peer_event_data._link_src = address.src;
    peer_event_data._link_dst = address.dst;

    if (is_connected) {
        _z_connectivity_peer_connected(manager->_parent->_session, &peer_event_data, mtu, is_streamed, is_reliable);
    } else {
        _z_connectivity_peer_disconnected(manager->_parent->_session, &peer_event_data, mtu, is_streamed, is_reliable);
    }
}
#endif

void _z_unicast_transport_manager_report_connected_event(
    _z_unicast_transport_manager_t *manager, _z_address_to_unicast_transport_peer_hmap_iter_t connected_peer_iter) {
    _z_session_t *session = manager->_parent->_session;

    if (session->_mode == Z_WHATAMI_CLIENT) {
        _z_interest_resend_client_declarations(session);
    } else {
        _z_interest_push_declarations_to_peer(session, connected_peer_iter);
    }

#if Z_FEATURE_CONNECTIVITY == 1
    _z_unicast_transport_manager_report_event(manager, connected_peer_iter, true);
#endif
}

void _z_unicast_transport_manager_report_disconnected_event(
    _z_unicast_transport_manager_t *manager, _z_address_to_unicast_transport_peer_hmap_iter_t disconnected_peer_iter) {
    _z_session_t *session = manager->_parent->_session;
    _z_interest_peer_disconnected(session, disconnected_peer_iter);
    _z_flush_remote_resources_for_peer(session, disconnected_peer_iter);
#if Z_FEATURE_LIVELINESS == 1 && Z_FEATURE_SUBSCRIPTION == 1
    _z_liveliness_subscription_undeclare_all(session, disconnected_peer_iter);
#endif
#if Z_FEATURE_CONNECTIVITY == 1
    _z_unicast_transport_manager_report_event(manager, disconnected_peer_iter, false);
#endif
}

void _z_unicast_transport_manager_fetch_zid(const _z_unicast_transport_manager_t *manager, _z_closure_zid_t *callback,
                                            z_what_t filter) {
    const _z_unicast_transport_peer_t *peer;
    _ZP_CONST_FOREACH_VAL_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_peers, peer,
                                    peer->_state == _Z_UNICAST_PEER_ESTABLISHED) {
        if (((z_what_t)peer->_remote_whatami & filter) != 0) {
            (*callback->call)(&peer->_remote_zid, callback->context);
        }
    }
}

bool _z_unicast_transport_manager_has_peer_with_zid(const _z_unicast_transport_manager_t *manager, const _z_id_t *zid) {
    const _z_unicast_transport_peer_t *peer;
    _ZP_CONST_FIND_VAL(_z_address_to_unicast_transport_peer_hmap, &manager->_peers, peer,
                       _->_state == _Z_UNICAST_PEER_ESTABLISHED && _z_id_eq(&_->_remote_zid, zid));
    return peer != NULL;
}

#if Z_FEATURE_CONNECTIVITY == 1
void _z_unicast_transport_manager_fetch_transports(const _z_unicast_transport_manager_t *manager,
                                                   z_closure_transport_callback_t call, void *arg) {
    const _z_unicast_transport_peer_t *peer;
    _ZP_CONST_FOREACH_VAL_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_peers, peer,
                                    peer->_state == _Z_UNICAST_PEER_ESTABLISHED) {
        _z_info_transport_t transport = {0};
        transport._zid = peer->_remote_zid;
        transport._whatami = (z_whatami_t)peer->_remote_whatami;
        transport._is_qos = false;
        transport._is_multicast = false;
        transport._is_shm = false;
        call(&transport, arg);
    }
}

void _z_unicast_transport_manager_fetch_links(const _z_unicast_transport_manager_t *manager,
                                              z_closure_link_callback_t call, void *arg,
                                              const _z_info_transport_t *opt_transport_filter) {
    const _z_unicast_transport_peer_t *peer;
    _ZP_CONST_FOREACH_VAL_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_peers, peer,
                                    peer->_state == _Z_UNICAST_PEER_ESTABLISHED) {
        if (opt_transport_filter != NULL &&
            (opt_transport_filter->_is_multicast || !_z_id_eq(&peer->_remote_zid, &opt_transport_filter->_zid))) {
            continue;
        }
        _z_info_link_t link = {0};
        link._is_reliable = _z_unicast_link_is_reliable(&peer->_link);
        link._is_streamed = _z_unicast_link_is_streamed(&peer->_link);
        link._mtu = _z_unicast_link_get_mtu(&peer->_link);
        link._zid = peer->_remote_zid;
        link._mcast_group = _z_string_null();
        _z_unicast_transport_peer_src_dst_address_t address;
        _z_unicast_transport_peer_src_dst_address_get(&peer->_link, &address);
        link._src = _z_string_alias(*_z_string_view_deref(&address.src));
        link._dst = _z_string_alias(*_z_string_view_deref(&address.dst));
        call(&link, arg);
    }
}

#endif
#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
