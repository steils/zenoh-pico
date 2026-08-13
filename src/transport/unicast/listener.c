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

#include "zenoh-pico/transport/unicast/listener.h"

#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/transport/peers.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/transport/unicast/tx.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1 && Z_FEATURE_UNICAST_PEER == 1 && \
    (Z_FEATURE_LINK_TCP == 1 || Z_FEATURE_LINK_TLS == 1)

static z_result_t _z_unicast_transport_listener_accept(_z_unicast_transport_listener_t *listener,
                                                       _z_unicast_link_t *link_out) {
    return _z_unicast_listener_accept(&listener->_listener, link_out);
}

z_result_t _z_unicast_transport_listener_listen(_z_unicast_transport_listener_t *listener) {
    _z_unicast_link_t connection_link;
    z_result_t ret = _z_unicast_transport_listener_accept(listener, &connection_link);
    if (ret == _Z_ERR_INVALID) {
        _Z_ERROR("Listener socket was closed");
        return ret;
    } else if (ret != _Z_RES_OK) {
        return Z_RETRY_LATER;
    }
    _z_session_t *session = listener->_manager->_parent->_session;
    // listen for handshake
    _z_transport_unicast_establish_param_t param = {0};
    ret = _z_unicast_handshake_listen(&param, &connection_link, &session->_local_zid, session->_mode);
    if (ret == _Z_RES_OK) {
        ret = _z_unicast_transport_manager_add_peer(listener->_manager, &param, &connection_link,
                                                    _ZP_CONNECT_PEER_ID_FROM_LISTEN, NULL);
        if (ret == _Z_RES_OK) {
            _Z_INFO("Accepted new unicast peer " _Z_ID_PRINT_FORMAT " on: %s", _Z_ID_PRINT_ARGS(&param._remote_zid),
                    *_z_config_listen_vec_at(&listener->_manager->_parent->_session->_config._listen,
                                             (size_t)listener->_locator_id));
            return _Z_RES_OK;
        } else {
            _z_unicast_link_clear(&connection_link);
        }
    } else {
        _z_unicast_link_clear(&connection_link);
        _Z_ERROR("Connection accept handshake failed with error %d", ret);
    }
    return Z_RETRY_LATER;
}

void _z_unicast_transport_listener_clear(_z_unicast_transport_listener_t *listener) {
    _z_unicast_listener_clear(&listener->_listener);
}

#endif
