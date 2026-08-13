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

#ifndef ZENOH_PICO_LINK_TRANSPORT_UDP_MULTICAST_H
#define ZENOH_PICO_LINK_TRANSPORT_UDP_MULTICAST_H

#include <stdint.h>

#include "zenoh-pico/link/address.h"
#include "zenoh-pico/link/transport/udp_unicast.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_LINK_UDP_MULTICAST == 1

static inline z_result_t _z_udp_multicast_default_endpoint_init_from_address(_z_sys_net_endpoint_t *ep,
                                                                             const _z_string_t *address) {
    return _z_udp_unicast_endpoint_init_from_address(ep, address);
}

static inline void _z_udp_multicast_default_endpoint_clear(_z_sys_net_endpoint_t *ep) {
    _z_udp_unicast_endpoint_clear(ep);
}

z_result_t _z_udp_multicast_endpoint_init_from_address(_z_sys_net_endpoint_t *ep, const _z_string_t *address);
void _z_udp_multicast_endpoint_clear(_z_sys_net_endpoint_t *ep);

// flawfinder: ignore
z_result_t _z_udp_multicast_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, _z_sys_net_endpoint_t *lep,
                                 uint32_t tout, const char *iface);
z_result_t _z_udp_multicast_listen(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout,
                                   const char *iface, const char *join);
void _z_udp_multicast_close(_z_sys_net_socket_t *sockrecv, _z_sys_net_socket_t *socksend,
                            const _z_sys_net_endpoint_t rep, const _z_sys_net_endpoint_t lep);
// flawfinder: ignore
size_t _z_udp_multicast_read(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len, const _z_sys_net_endpoint_t lep,
                             _z_link_address_t *ep_out);
size_t _z_udp_multicast_write(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len,
                              const _z_sys_net_endpoint_t rep);

z_result_t _z_endpoint_udp_multicast_valid(const _z_endpoint_t *ep);

typedef struct {
    _z_sys_net_socket_t _sock;
    _z_sys_net_socket_t _msock;
    _z_sys_net_endpoint_t _rep;
    _z_sys_net_endpoint_t _lep;
} _z_multicast_link_udp_t;

bool _z_multicast_link_udp_read(_z_multicast_link_udp_t *udp, uint8_t *ptr, size_t *len, _z_link_address_t *addr_out);
bool _z_multicast_link_udp_write(_z_multicast_link_udp_t *udp, const uint8_t *ptr, size_t *len);
z_result_t _z_multicast_link_udp_create(_z_multicast_link_udp_t *udp, const _z_endpoint_t *endpoint);
void _z_multicast_link_udp_clear(_z_multicast_link_udp_t *udp);
uint16_t _z_multicast_link_udp_get_mtu(const _z_multicast_link_udp_t *udp);
bool _z_multicast_link_udp_is_reliable(const _z_multicast_link_udp_t *udp);
bool _z_multicast_link_udp_is_streamed(const _z_multicast_link_udp_t *udp);
_z_sys_net_socket_t *_z_multicast_link_udp_get_sock(_z_multicast_link_udp_t *udp);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_UDP_MULTICAST_H */
