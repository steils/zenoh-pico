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

#include <stddef.h>
#include <stdlib.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/udp_multicast.h"
#include "zenoh-pico/link/transport/udp_unicast.h"

#if Z_FEATURE_LINK_UDP_MULTICAST == 1

z_result_t _z_endpoint_udp_multicast_valid(const _z_endpoint_t *endpoint) {
    _z_string_t udp_str = _z_string_alias_str(UDP_SCHEMA);
    if (!_z_string_equals(&endpoint->_locator._protocol, &udp_str)) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    z_result_t ret = _z_udp_unicast_address_valid(&endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return ret;
    }

    const char *iface = _z_str_intmap_get(&endpoint->_config, UDP_CONFIG_IFACE_KEY);
    if (iface == NULL) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    return _Z_RES_OK;
}

bool _z_multicast_link_udp_read(_z_multicast_link_udp_t *udp, uint8_t *ptr, size_t *len, _z_link_address_t *addr_out) {
    *len = _z_udp_multicast_read(udp->_sock, ptr, *len, udp->_lep, addr_out);
    return *len != SIZE_MAX;
}
bool _z_multicast_link_udp_write(_z_multicast_link_udp_t *udp, const uint8_t *ptr, size_t *len) {
    *len = _z_udp_multicast_write(udp->_msock, ptr, *len, udp->_rep);
    return *len != SIZE_MAX;
}

z_result_t _z_multicast_link_udp_create(_z_multicast_link_udp_t *udp, const _z_endpoint_t *endpoint) {
    memset(udp, 0, sizeof(_z_multicast_link_udp_t));
    uint32_t tout = Z_CONFIG_SOCKET_TIMEOUT;
    char *tout_as_str = _z_str_intmap_get(&endpoint->_config, UDP_CONFIG_TOUT_KEY);
    if (tout_as_str != NULL) {
        tout = (uint32_t)strtoul(tout_as_str, NULL, 10);
    }
    const char *iface = _z_str_intmap_get(&endpoint->_config, UDP_CONFIG_IFACE_KEY);
    const char *join = _z_str_intmap_get(&endpoint->_config, UDP_CONFIG_JOIN_KEY);
    _Z_RETURN_IF_ERR(_z_udp_unicast_endpoint_init_from_address(&udp->_rep, &endpoint->_locator._address));
    _Z_CLEAN_RETURN_IF_ERR(_z_udp_multicast_listen(&udp->_sock, udp->_rep, tout, iface, join),
                           _z_udp_unicast_endpoint_clear(&udp->_rep));

    _Z_CLEAN_RETURN_IF_ERR(_z_udp_multicast_open(&udp->_msock, udp->_rep, &udp->_lep, tout, iface),
                           _z_multicast_link_udp_clear(udp));
    return _Z_RES_OK;
}

void _z_multicast_link_udp_clear(_z_multicast_link_udp_t *udp) {
    _z_udp_multicast_close(&udp->_sock, &udp->_msock, udp->_rep, udp->_lep);
    _z_udp_multicast_endpoint_clear(&udp->_lep);
    _z_udp_multicast_endpoint_clear(&udp->_rep);
}

uint16_t _z_multicast_link_udp_get_mtu(const _z_multicast_link_udp_t *udp) {
    _ZP_UNUSED(udp);
    return Z_UDP_MTU;  // @TODO: the return value should change depending on the target platform.
}

bool _z_multicast_link_udp_is_reliable(const _z_multicast_link_udp_t *udp) {
    _ZP_UNUSED(udp);
    return false;
}

bool _z_multicast_link_udp_is_streamed(const _z_multicast_link_udp_t *udp) {
    _ZP_UNUSED(udp);
    return false;
}

_z_sys_net_socket_t *_z_multicast_link_udp_get_sock(_z_multicast_link_udp_t *udp) { return &udp->_sock; }

#endif
