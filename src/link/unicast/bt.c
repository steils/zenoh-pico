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

#include "zenoh-pico/link/config/bt.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/bt.h"

#if Z_FEATURE_LINK_BLUETOOTH == 1

#define SPP_MAXIMUM_PAYLOAD 128

bool _z_unicast_link_bt_read(_z_unicast_link_bt_t *bt, uint8_t *ptr, size_t *len) {
    *len = _z_read_bt(bt->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

bool _z_unicast_link_bt_write(_z_unicast_link_bt_t *bt, const uint8_t *ptr, size_t *len) {
    *len = _z_send_bt(bt->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

z_result_t _z_unicast_link_bt_create(_z_unicast_link_bt_t *bt, const _z_endpoint_t *endpoint) {
    bt->_gname = _z_convert_address_bt(&endpoint->_locator._address);
    if (bt->_gname == NULL) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    const char *mode_str = _z_str_intmap_get(&endpoint->_config, BT_CONFIG_MODE_KEY);
    uint8_t mode = (strcmp(mode_str, "master") == 0) ? _Z_BT_MODE_MASTER : _Z_BT_MODE_SLAVE;
    const char *profile_str = _z_str_intmap_get(&endpoint->_config, BT_CONFIG_PROFILE_KEY);
    uint8_t profile = (strcmp(profile_str, "spp") == 0) ? _Z_BT_PROFILE_SPP : _Z_BT_PROFILE_UNSUPPORTED;
    uint32_t tout = Z_CONFIG_SOCKET_TIMEOUT;
    char *tout_as_str = _z_str_intmap_get(&endpoint->_config, BT_CONFIG_TOUT_KEY);
    if (tout_as_str != NULL) {
        tout = (uint32_t)strtoul(tout_as_str, NULL, 10);
    }

    z_result_t ret = _z_open_bt(&bt->_sock, bt->_gname, mode, profile, tout);
    if (ret != _Z_RES_OK) {
        z_free(bt->_gname);
        bt->_gname = NULL;
    }
    return ret;
}

void _z_unicast_link_bt_clear(_z_unicast_link_bt_t *bt) {
    _z_close_bt(&bt->_sock);
    z_free(bt->_gname);
}

uint16_t _z_unicast_link_bt_get_mtu(const _z_unicast_link_bt_t *bt) {
    _ZP_UNUSED(bt);
    return SPP_MAXIMUM_PAYLOAD;
}

bool _z_unicast_link_bt_is_reliable(const _z_unicast_link_bt_t *bt) {
    _ZP_UNUSED(bt);
    return false;
}

bool _z_unicast_link_bt_is_streamed(const _z_unicast_link_bt_t *bt) {
    _ZP_UNUSED(bt);
    return true;
}

z_result_t _z_unicast_link_bt_get_endpoints(const _z_unicast_link_bt_t *bt, char *local, size_t local_len, char *remote,
                                            size_t remote_len) {
    return _z_socket_get_endpoints(&bt->_sock, local, local_len, remote, remote_len);
}

_z_sys_net_socket_t *_z_unicast_link_bt_get_sock(_z_unicast_link_bt_t *bt) { return &bt->_sock; }

#endif
