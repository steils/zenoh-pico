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

#include "zenoh-pico/link/config/udp.h"

#include <stdint.h>
#include <stdlib.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/udp_unicast.h"

#if Z_FEATURE_LINK_UDP_UNICAST == 1

z_result_t _z_endpoint_udp_unicast_valid(const _z_endpoint_t *endpoint) {
    _z_string_t udp_str = _z_string_alias_str(UDP_SCHEMA);
    if (!_z_string_equals(&endpoint->_locator._protocol, &udp_str)) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    z_result_t ret = _z_udp_unicast_address_valid(&endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
    }
    return ret;
}

bool _z_unicast_link_udp_read(_z_unicast_link_udp_t *udp, uint8_t *ptr, size_t *len) {
    *len = _z_udp_unicast_read(udp->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

bool _z_unicast_link_udp_write(_z_unicast_link_udp_t *udp, const uint8_t *ptr, size_t *len) {
    *len = _z_udp_unicast_write(udp->_sock, ptr, *len, udp->_rep);
    return *len != SIZE_MAX;
}

z_result_t _z_unicast_link_udp_create(_z_unicast_link_udp_t *udp, const _z_endpoint_t *endpoint) {
    memset(udp, 0, sizeof(_z_unicast_link_udp_t));
    uint32_t tout = Z_CONFIG_SOCKET_TIMEOUT;
    char *tout_as_str = _z_str_intmap_get(&endpoint->_config, UDP_CONFIG_TOUT_KEY);
    if (tout_as_str != NULL) {
        tout = (uint32_t)strtoul(tout_as_str, NULL, 10);
    }
    _Z_RETURN_IF_ERR(_z_udp_unicast_endpoint_init_from_address(&udp->_rep, &endpoint->_locator._address));
    _Z_CLEAN_RETURN_IF_ERR(_z_udp_unicast_open(&udp->_sock, udp->_rep, tout),
                           _z_udp_unicast_endpoint_clear(&udp->_rep));
    return _Z_RES_OK;
}

void _z_unicast_link_udp_clear(_z_unicast_link_udp_t *udp) {
    _z_udp_unicast_endpoint_clear(&udp->_rep);
    _z_udp_unicast_close(&udp->_sock);
}

uint16_t _z_unicast_link_udp_get_mtu(const _z_unicast_link_udp_t *udp) {
    _ZP_UNUSED(udp);
    // @TODO: the return value should change depending on the target platform.
    return Z_UDP_MTU;
}

bool _z_unicast_link_udp_is_reliable(const _z_unicast_link_udp_t *udp) {
    _ZP_UNUSED(udp);
    return false;
}

bool _z_unicast_link_udp_is_streamed(const _z_unicast_link_udp_t *udp) {
    _ZP_UNUSED(udp);
    return false;
}

z_result_t _z_unicast_link_udp_get_endpoints(const _z_unicast_link_udp_t *udp, char *local, size_t local_len,
                                             char *remote, size_t remote_len) {
    return _z_socket_get_endpoints(&udp->_sock, local, local_len, remote, remote_len);
}

_z_sys_net_socket_t *_z_unicast_link_udp_get_sock(_z_unicast_link_udp_t *udp) { return &udp->_sock; }

#endif
