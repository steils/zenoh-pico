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

#ifndef ZENOH_PICO_LINK_TRANSPORT_SERIAL_PROTOCOL_H
#define ZENOH_PICO_LINK_TRANSPORT_SERIAL_PROTOCOL_H

#include <stdint.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_LINK_SERIAL == 1

#define _Z_SERIAL_MTU_SIZE 1500
#define _Z_SERIAL_MFS_SIZE _Z_SERIAL_MTU_SIZE + 1 + 2 + 4  // MTU + Header + Serial Len + Serial CRC32
#define _Z_SERIAL_MAX_COBS_BUF_SIZE \
    1516  // Max On-the-wire length for an MFS/MTU of 1510/1500 (MFS + Overhead Byte (OHB) + End of packet (EOP))

z_result_t _z_serial_endpoint_valid(const _z_endpoint_t *endpoint);
z_result_t _z_serial_protocol_open(_z_sys_net_socket_t *sock, const _z_endpoint_t *endpoint, bool connect);
void _z_serial_protocol_close(_z_sys_net_socket_t *sock);
z_result_t _z_connect_serial(const _z_sys_net_socket_t sock);
size_t _z_read_serial(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len);
size_t _z_send_serial(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len);
z_result_t _z_endpoint_serial_valid(const _z_endpoint_t *ep);

typedef struct {
    _z_sys_net_socket_t _sock;
} _z_unicast_link_serial_t;

bool _z_unicast_link_serial_read(_z_unicast_link_serial_t *serial, uint8_t *ptr, size_t *len);
bool _z_unicast_link_serial_write(_z_unicast_link_serial_t *serial, const uint8_t *ptr, size_t *len);
// z_result_t _z_unicast_listener_serial_create(_z_unicast_listener_serial_t *listener, const _z_endpoint_t *endpoint);
// z_result_t _z_unicast_listener_serial_accept(_z_unicast_listener_serial_t *listener, _z_unicast_link_serial_t *link);
// void _z_unicast_listener_serial_clear(_z_unicast_listener_serial_t *listener);
z_result_t _z_unicast_link_serial_create(_z_unicast_link_serial_t *serial, const _z_endpoint_t *endpoint);
void _z_unicast_link_serial_clear(_z_unicast_link_serial_t *serial);
uint16_t _z_unicast_link_serial_get_mtu(const _z_unicast_link_serial_t *serial);
bool _z_unicast_link_serial_is_reliable(const _z_unicast_link_serial_t *serial);
bool _z_unicast_link_serial_is_streamed(const _z_unicast_link_serial_t *serial);
z_result_t _z_unicast_link_serial_get_endpoints(const _z_unicast_link_serial_t *serial, char *local, size_t local_len,
                                                char *remote, size_t remote_len);
_z_sys_net_socket_t *_z_unicast_link_serial_get_sock(_z_unicast_link_serial_t *serial);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_SERIAL_PROTOCOL_H */
