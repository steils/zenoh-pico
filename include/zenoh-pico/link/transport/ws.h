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

#ifndef ZENOH_PICO_LINK_TRANSPORT_WS_H
#define ZENOH_PICO_LINK_TRANSPORT_WS_H

#include <stdint.h>

#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_LINK_WS == 1

z_result_t _z_ws_endpoint_init(_z_sys_net_endpoint_t *ep, const _z_string_t *address);
void _z_ws_endpoint_clear(_z_sys_net_endpoint_t *ep);
z_result_t _z_ws_transport_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint, uint32_t tout);
void _z_ws_transport_close(_z_sys_net_socket_t *sock);
size_t _z_ws_transport_read(const _z_sys_net_socket_t *sock, uint8_t *ptr, size_t len);
size_t _z_ws_transport_write(const _z_sys_net_socket_t *sock, const uint8_t *ptr, size_t len);

z_result_t _z_endpoint_ws_valid(const _z_endpoint_t *ep);

typedef struct _z_unicast_link_ws_t {
    _z_sys_net_socket_t _sock;
} _z_unicast_link_ws_t;

bool _z_unicast_link_ws_read(_z_unicast_link_ws_t *tcp, uint8_t *ptr, size_t *len);
bool _z_unicast_link_ws_write(_z_unicast_link_ws_t *ws, const uint8_t *ptr, size_t *len);
// z_result_t _z_unicast_listener_ws_create(_z_unicast_listener_ws_t *listener, const _z_endpoint_t *endpoint, const
// _z_config_t *config); z_result_t _z_unicast_listener_ws_accept(_z_unicast_listener_ws_t *listener,
// _z_unicast_link_ws_t *link);
z_result_t _z_unicast_link_ws_create(_z_unicast_link_ws_t *ws, const _z_endpoint_t *endpoint);
void _z_unicast_link_ws_clear(_z_unicast_link_ws_t *ws);
// void _z_unicast_listener_ws_clear(_z_unicast_listener_ws_t *listener);
uint16_t _z_unicast_link_ws_get_mtu(const _z_unicast_link_ws_t *ws);
bool _z_unicast_link_ws_is_reliable(const _z_unicast_link_ws_t *ws);
bool _z_unicast_link_ws_is_streamed(const _z_unicast_link_ws_t *ws);
z_result_t _z_unicast_link_ws_get_endpoints(const _z_unicast_link_ws_t *ws, char *local, size_t local_len, char *remote,
                                            size_t remote_len);
_z_sys_net_socket_t *_z_unicast_link_ws_get_sock(_z_unicast_link_ws_t *ws);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_WS_H */
