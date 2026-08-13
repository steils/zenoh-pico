//
// Copyright (c) 2022 ZettaScale Technology
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

#include "zenoh-pico/link/config/ws.h"

#include <stddef.h>
#include <stdlib.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/tcp.h"
#include "zenoh-pico/link/transport/ws.h"

#if Z_FEATURE_LINK_WS == 1

static z_result_t _z_ws_address_valid(const _z_string_t *address) { return _z_tcp_address_valid(address); }

z_result_t _z_endpoint_ws_valid(const _z_endpoint_t *endpoint) {
    _z_string_t str = _z_string_alias_str(WS_SCHEMA);
    if (!_z_string_equals(&endpoint->_locator._protocol, &str)) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    z_result_t ret = _z_ws_address_valid(&endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
    }
    return ret;
}

bool _z_unicast_link_ws_read(_z_unicast_link_ws_t *tcp, uint8_t *ptr, size_t *len) {
    *len = _z_ws_transport_read(&tcp->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

bool _z_unicast_link_ws_write(_z_unicast_link_ws_t *ws, const uint8_t *ptr, size_t *len) {
    *len = _z_ws_transport_write(&ws->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

z_result_t _z_unicast_link_ws_create(_z_unicast_link_ws_t *ws, const _z_endpoint_t *endpoint) {
    memset(ws, 0, sizeof(_z_unicast_link_ws_t));
    _z_sys_net_endpoint_t ep;
    z_result_t ret = _z_ws_endpoint_init(&ep, &endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    uint32_t tout = Z_CONFIG_SOCKET_TIMEOUT;
    char *tout_as_str = _z_str_intmap_get(&endpoint->_config, WS_CONFIG_TOUT_KEY);
    if (tout_as_str != NULL) {
        tout = (uint32_t)strtoul(tout_as_str, NULL, 10);
    }

    ret = _z_ws_transport_open(&ws->_sock, ep, tout);
    _z_ws_endpoint_clear(&ep);
    return ret;
}

void _z_unicast_link_ws_clear(_z_unicast_link_ws_t *ws) { _z_ws_transport_close(&ws->_sock); }

uint16_t _z_unicast_link_ws_get_mtu(const _z_unicast_link_ws_t *ws) {
    _ZP_UNUSED(ws);
    return 65535;  // Maximum MTU for TCP
}
bool _z_unicast_link_ws_is_reliable(const _z_unicast_link_ws_t *ws) {
    _ZP_UNUSED(ws);
    return true;
}
bool _z_unicast_link_ws_is_streamed(const _z_unicast_link_ws_t *ws) {
    _ZP_UNUSED(ws);
    return false;
}

z_result_t _z_unicast_link_ws_get_endpoints(const _z_unicast_link_ws_t *ws, char *local, size_t local_len, char *remote,
                                            size_t remote_len) {
    return _z_socket_get_endpoints(&ws->_sock, local, local_len, remote, remote_len);
}

_z_sys_net_socket_t *_z_unicast_link_ws_get_sock(_z_unicast_link_ws_t *ws) { return &ws->_sock; }

#endif
