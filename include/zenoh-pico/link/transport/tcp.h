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

#ifndef ZENOH_PICO_LINK_TRANSPORT_TCP_H
#define ZENOH_PICO_LINK_TRANSPORT_TCP_H

#include <stddef.h>
#include <stdint.h>

#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

char *_z_tcp_address_parse_host(const _z_string_t *address);
z_result_t _z_tcp_address_valid(const _z_string_t *address);
z_result_t _z_tcp_endpoint_init(_z_sys_net_endpoint_t *ep, const char *address, const char *port);
void _z_tcp_endpoint_clear(_z_sys_net_endpoint_t *ep);
z_result_t _z_tcp_endpoint_init_from_address(_z_sys_net_endpoint_t *ep, const _z_string_t *address);

// flawfinder: ignore
z_result_t _z_tcp_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint, uint32_t tout);
z_result_t _z_tcp_listen(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint);
z_result_t _z_tcp_accept(const _z_sys_net_socket_t *sock_in, _z_sys_net_socket_t *sock_out);
void _z_tcp_close(_z_sys_net_socket_t *sock);

// flawfinder: ignore
size_t _z_tcp_read(_z_sys_net_socket_t sock, uint8_t *ptr, size_t len);
size_t _z_tcp_write(_z_sys_net_socket_t sock, const uint8_t *ptr, size_t len);
z_result_t _z_endpoint_tcp_valid(const _z_endpoint_t *endpoint);

typedef struct _z_unicast_link_tcp_t {
    _z_sys_net_socket_t _sock;
} _z_unicast_link_tcp_t;

typedef struct _z_unicast_listener_tcp_t {
    _z_sys_net_socket_t _sock;
} _z_unicast_listener_tcp_t;

bool _z_unicast_link_tcp_read(_z_unicast_link_tcp_t *tcp, uint8_t *ptr, size_t *len);
bool _z_unicast_link_tcp_write(_z_unicast_link_tcp_t *tcp, const uint8_t *ptr, size_t *len);
z_result_t _z_unicast_listener_tcp_create(_z_unicast_listener_tcp_t *listener, const _z_endpoint_t *endpoint);
z_result_t _z_unicast_listener_tcp_accept(_z_unicast_listener_tcp_t *listener, _z_unicast_link_tcp_t *link);
z_result_t _z_unicast_link_tcp_create(_z_unicast_link_tcp_t *tcp, const _z_endpoint_t *endpoint);
void _z_unicast_link_tcp_clear(_z_unicast_link_tcp_t *tcp);
void _z_unicast_listener_tcp_clear(_z_unicast_listener_tcp_t *listener);
uint16_t _z_unicast_link_tcp_get_mtu(const _z_unicast_link_tcp_t *tcp);
bool _z_unicast_link_tcp_is_reliable(const _z_unicast_link_tcp_t *tcp);
bool _z_unicast_link_tcp_is_streamed(const _z_unicast_link_tcp_t *tcp);
z_result_t _z_unicast_link_tcp_get_endpoints(const _z_unicast_link_tcp_t *tcp, char *local, size_t local_len,
                                             char *remote, size_t remote_len);
_z_sys_net_socket_t *_z_unicast_link_tcp_get_sock(_z_unicast_link_tcp_t *tcp);
#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_TCP_H */
