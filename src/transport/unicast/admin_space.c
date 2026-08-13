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

#include "zenoh-pico/transport/unicast/admin_space.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/unicast/transport.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1
#if Z_FEATURE_ADMIN_SPACE == 1
z_result_t _z_unicast_transport_manager_encode_locators_json(const _z_unicast_transport_manager_t *manager,
                                                             _z_json_encoder_t *je) {
#if Z_FEATURE_UNICAST_PEER == 1
    const _z_unicast_transport_listener_t *listener = NULL;
    _ZP_CONST_FOREACH (_z_unicast_transport_listener_vec, &manager->_listeners, listener) {
        const char *locator =
            *_z_config_listen_vec_at(&manager->_parent->_session->_config._listen, (size_t)listener->_locator_id);
        _Z_RETURN_IF_ERR(_z_json_encoder_write_string(je, locator));
    }
    return _Z_RES_OK;
#else
    (void)manager;
    (void)je;
    return _Z_RES_OK;
#endif
}

z_result_t _z_unicast_transport_manager_encode_transports_json(const _z_unicast_transport_manager_t *manager,
                                                               _z_json_encoder_t *je) {
    const _z_unicast_transport_peer_t *peer = NULL;
    _ZP_CONST_FOREACH (_z_unicast_transport_peer_hset, &manager->_peers, peer) {
        _Z_RETURN_IF_ERR(_z_json_encoder_start_object(je));

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "peer"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_zid(je, &peer->_remote_zid));

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "whatami"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_whatami(je, (z_whatami_t)peer->_remote_whatami));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "shm"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_boolean(je, false));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "weight"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_null(je));
        // TODO: region

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "links"));
        _Z_RETURN_IF_ERR(_z_json_encoder_start_array(je));
        _Z_RETURN_IF_ERR(_z_json_encoder_start_object(je));
        _z_unicast_transport_peer_src_dst_address_t address;
        _z_unicast_transport_peer_src_dst_address_get(peer, &address);
        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "src"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_z_string(je, _z_string_view_deref(&address.src)));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "dst"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_z_string(je, _z_string_view_deref(&address.dst)));
        _Z_RETURN_IF_ERR(_z_json_encoder_end_object(je));
        _Z_RETURN_IF_ERR(_z_json_encoder_end_array(je));

        _Z_RETURN_IF_ERR(_z_json_encoder_end_object(je));
    }
    return _Z_RES_OK;
}
#endif
#endif
