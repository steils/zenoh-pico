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

#ifndef ZENOH_PICO_LINK_TRANSPORT_UDP_UNICAST_H
#define ZENOH_PICO_LINK_TRANSPORT_UDP_UNICAST_H

#include <stddef.h>
#include <stdint.h>

#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

z_result_t _z_udp_unicast_address_valid(const _z_string_t *address);
z_result_t _z_udp_unicast_endpoint_init(_z_sys_net_endpoint_t *ep, const char *address, const char *port);
void _z_udp_unicast_endpoint_clear(_z_sys_net_endpoint_t *ep);
z_result_t _z_udp_unicast_endpoint_init_from_address(_z_sys_net_endpoint_t *ep, const _z_string_t *address);

// flawfinder: ignore
z_result_t _z_udp_unicast_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint, uint32_t tout);
z_result_t _z_udp_unicast_listen(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t endpoint, uint32_t tout);
void _z_udp_unicast_close(_z_sys_net_socket_t *sock);

// flawfinder: ignore
size_t _z_udp_unicast_read(_z_sys_net_socket_t sock, uint8_t *ptr, size_t len);
size_t _z_udp_unicast_write(_z_sys_net_socket_t sock, const uint8_t *ptr, size_t len,
                            const _z_sys_net_endpoint_t endpoint);
z_result_t _z_endpoint_udp_unicast_valid(const _z_endpoint_t *ep);

typedef struct {
    _z_sys_net_socket_t _sock;
    _z_sys_net_endpoint_t _rep;
} _z_unicast_link_udp_t;

bool _z_unicast_link_udp_read(_z_unicast_link_udp_t *udp, uint8_t *ptr, size_t *len);
bool _z_unicast_link_udp_write(_z_unicast_link_udp_t *udp, const uint8_t *ptr, size_t *len);
// z_result_t _z_unicast_listener_udp_create(_z_unicast_listener_udp_t *listener, const _z_endpoint_t *endpoint);
// z_result_t _z_unicast_listener_udp_accept(_z_unicast_listener_udp_t *listener, _z_unicast_link_udp_t *link);
// void _z_unicast_listener_udp_clear(_z_unicast_listener_udp_t *listener);
z_result_t _z_unicast_link_udp_create(_z_unicast_link_udp_t *udp, const _z_endpoint_t *endpoint);
void _z_unicast_link_udp_clear(_z_unicast_link_udp_t *udp);
uint16_t _z_unicast_link_udp_get_mtu(const _z_unicast_link_udp_t *udp);
bool _z_unicast_link_udp_is_reliable(const _z_unicast_link_udp_t *udp);
bool _z_unicast_link_udp_is_streamed(const _z_unicast_link_udp_t *udp);
z_result_t _z_unicast_link_udp_get_endpoints(const _z_unicast_link_udp_t *udp, char *local, size_t local_len,
                                             char *remote, size_t remote_len);
_z_sys_net_socket_t *_z_unicast_link_udp_get_sock(_z_unicast_link_udp_t *udp);
#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_UDP_UNICAST_H */
