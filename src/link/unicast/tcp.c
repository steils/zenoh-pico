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

#include "zenoh-pico/link/config/tcp.h"

#include <stdlib.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/tcp.h"

#if Z_FEATURE_LINK_TCP == 1

z_result_t _z_endpoint_tcp_valid(const _z_endpoint_t *endpoint) {
    _z_string_t tcp_str = _z_string_alias_str(TCP_SCHEMA);
    if (!_z_string_equals(&endpoint->_locator._protocol, &tcp_str)) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    z_result_t ret = _z_tcp_address_valid(&endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
    }
    return ret;
}

bool _z_unicast_link_tcp_read(_z_unicast_link_tcp_t *tcp, uint8_t *ptr, size_t *len) {
    *len = _z_tcp_read(tcp->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

bool _z_unicast_link_tcp_write(_z_unicast_link_tcp_t *tcp, const uint8_t *ptr, size_t *len) {
    *len = _z_tcp_write(tcp->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

z_result_t _z_unicast_listener_tcp_create(_z_unicast_listener_tcp_t *listener, const _z_endpoint_t *endpoint) {
    memset(listener, 0, sizeof(_z_unicast_listener_tcp_t));
    _z_sys_net_endpoint_t ep;
    _Z_RETURN_IF_ERR(_z_tcp_endpoint_init_from_address(&ep, &endpoint->_locator._address));
    z_result_t ret = _z_tcp_listen(&listener->_sock, ep);
    _z_tcp_endpoint_clear(&ep);
    return ret;
}

z_result_t _z_unicast_listener_tcp_accept(_z_unicast_listener_tcp_t *listener, _z_unicast_link_tcp_t *link) {
    memset(link, 0, sizeof(_z_unicast_link_tcp_t));
    _z_socket_set_blocking(&listener->_sock, false);
    z_result_t ret = _z_tcp_accept(&listener->_sock, &link->_sock);
    if (ret == _Z_RES_OK) {
        _z_socket_set_blocking(&link->_sock, true);
    }
    _z_socket_set_blocking(&listener->_sock, true);
    return ret;
}

z_result_t _z_unicast_link_tcp_create(_z_unicast_link_tcp_t *tcp, const _z_endpoint_t *endpoint) {
    memset(tcp, 0, sizeof(_z_unicast_link_tcp_t));
    _z_sys_net_endpoint_t ep;
    _Z_RETURN_IF_ERR(_z_tcp_endpoint_init_from_address(&ep, &endpoint->_locator._address));
    z_result_t ret = _z_tcp_open(&tcp->_sock, ep, Z_CONFIG_SOCKET_TIMEOUT);
    if (ret == _Z_RES_OK) {
        _z_socket_set_blocking(&tcp->_sock, true);
    }
    _z_tcp_endpoint_clear(&ep);
    return ret;
}

void _z_unicast_link_tcp_clear(_z_unicast_link_tcp_t *tcp) { _z_tcp_close(&tcp->_sock); }

void _z_unicast_listener_tcp_clear(_z_unicast_listener_tcp_t *listener) { _z_tcp_close(&listener->_sock); }

uint16_t _z_unicast_link_tcp_get_mtu(const _z_unicast_link_tcp_t *tcp) {
    _ZP_UNUSED(tcp);
    return 65535;  // Maximum MTU for TCP
}

bool _z_unicast_link_tcp_is_reliable(const _z_unicast_link_tcp_t *tcp) {
    _ZP_UNUSED(tcp);
    return true;
}

bool _z_unicast_link_tcp_is_streamed(const _z_unicast_link_tcp_t *tcp) {
    _ZP_UNUSED(tcp);
    return true;
}

z_result_t _z_unicast_link_tcp_get_endpoints(const _z_unicast_link_tcp_t *tcp, char *local, size_t local_len,
                                             char *remote, size_t remote_len) {
    return _z_socket_get_endpoints(&tcp->_sock, local, local_len, remote, remote_len);
}

_z_sys_net_socket_t *_z_unicast_link_tcp_get_sock(_z_unicast_link_tcp_t *tcp) { return &tcp->_sock; }

#endif
