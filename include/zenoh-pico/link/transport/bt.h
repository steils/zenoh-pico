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

#ifndef ZENOH_PICO_LINK_TRANSPORT_BT_H
#define ZENOH_PICO_LINK_TRANSPORT_BT_H

#include <stdint.h>

#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/link/address.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/result.h"

#if Z_FEATURE_LINK_BLUETOOTH == 1

#ifdef __cplusplus
extern "C" {
#endif

#define _Z_BT_MODE_MASTER 0
#define _Z_BT_MODE_SLAVE 1

#define _Z_BT_PROFILE_UNSUPPORTED 255
#define _Z_BT_PROFILE_SPP 0

z_result_t _z_open_bt(_z_sys_net_socket_t *sock, const char *gname, uint8_t mode, uint8_t profile, uint32_t tout);
z_result_t _z_listen_bt(_z_sys_net_socket_t *sock, const char *gname, uint8_t mode, uint8_t profile, uint32_t tout);
void _z_close_bt(_z_sys_net_socket_t *sock);
size_t _z_read_bt(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len);
size_t _z_send_bt(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len);
char *_z_convert_address_bt(_z_string_t *address);
z_result_t _z_endpoint_bt_multicast_valid(const _z_endpoint_t *ep);
static inline z_result_t _z_endpoint_bt_unicast_valid(const _z_endpoint_t *ep) {
    return _z_endpoint_bt_multicast_valid(ep);
}

typedef struct _z_multicast_link_bt_t {
    _z_sys_net_socket_t _sock;
    char *_gname;
} _z_multicast_link_bt_t;

bool _z_multicast_link_bt_read(_z_multicast_link_bt_t *bt, uint8_t *ptr, size_t *len, _z_link_address_t *addr_out);
bool _z_multicast_link_bt_write(_z_multicast_link_bt_t *bt, const uint8_t *ptr, size_t *len);
z_result_t _z_multicast_link_bt_create(_z_multicast_link_bt_t *bt, const _z_endpoint_t *endpoint);
void _z_multicast_link_bt_clear(_z_multicast_link_bt_t *bt);
uint16_t _z_multicast_link_bt_get_mtu(const _z_multicast_link_bt_t *bt);
bool _z_multicast_link_bt_is_reliable(const _z_multicast_link_bt_t *bt);
bool _z_multicast_link_bt_is_streamed(const _z_multicast_link_bt_t *bt);
_z_sys_net_socket_t *_z_multicast_link_bt_get_sock(_z_multicast_link_bt_t *bt);

typedef struct {
    _z_sys_net_socket_t _sock;
    char *_gname;
} _z_unicast_link_bt_t;

bool _z_unicast_link_bt_read(_z_unicast_link_bt_t *bt, uint8_t *ptr, size_t *len);
bool _z_unicast_link_bt_write(_z_unicast_link_bt_t *bt, const uint8_t *ptr, size_t *len);
z_result_t _z_unicast_link_bt_create(_z_unicast_link_bt_t *bt, const _z_endpoint_t *endpoint);
void _z_unicast_link_bt_clear(_z_unicast_link_bt_t *bt);
uint16_t _z_unicast_link_bt_get_mtu(const _z_unicast_link_bt_t *bt);
bool _z_unicast_link_bt_is_reliable(const _z_unicast_link_bt_t *bt);
bool _z_unicast_link_bt_is_streamed(const _z_unicast_link_bt_t *bt);
z_result_t _z_unicast_link_bt_get_endpoints(const _z_unicast_link_bt_t *bt, char *local, size_t local_len, char *remote,
                                            size_t remote_len);
_z_sys_net_socket_t *_z_unicast_link_bt_get_sock(_z_unicast_link_bt_t *bt);
#ifdef __cplusplus
}
#endif

#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_BT_H */
