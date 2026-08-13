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

#include "zenoh-pico/link/transport/serial.h"

#include <stddef.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/serial_protocol.h"

#if Z_FEATURE_LINK_SERIAL == 1

z_result_t _z_endpoint_serial_valid(const _z_endpoint_t *endpoint) { return _z_serial_endpoint_valid(endpoint); }

bool _z_unicast_link_serial_read(_z_unicast_link_serial_t *serial, uint8_t *ptr, size_t *len) {
    *len = _z_read_serial(serial->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

bool _z_unicast_link_serial_write(_z_unicast_link_serial_t *serial, const uint8_t *ptr, size_t *len) {
    *len = _z_send_serial(serial->_sock, ptr, *len);
    return *len != SIZE_MAX;
}

z_result_t _z_unicast_link_serial_create(_z_unicast_link_serial_t *serial, const _z_endpoint_t *endpoint) {
    return _z_serial_protocol_open(&serial->_sock, endpoint, true);
}

void _z_unicast_link_serial_clear(_z_unicast_link_serial_t *serial) { _z_serial_close(&serial->_sock); }

uint16_t _z_unicast_link_serial_get_mtu(const _z_unicast_link_serial_t *serial) {
    _ZP_UNUSED(serial);
    return _Z_SERIAL_MTU_SIZE;
}

bool _z_unicast_link_serial_is_reliable(const _z_unicast_link_serial_t *serial) {
    _ZP_UNUSED(serial);
    return false;
}

bool _z_unicast_link_serial_is_streamed(const _z_unicast_link_serial_t *serial) {
    _ZP_UNUSED(serial);
    return false;
}

z_result_t _z_unicast_link_serial_get_endpoints(const _z_unicast_link_serial_t *serial, char *local, size_t local_len,
                                                char *remote, size_t remote_len) {
    return _z_socket_get_endpoints(&serial->_sock, local, local_len, remote, remote_len);
}

_z_sys_net_socket_t *_z_unicast_link_serial_get_sock(_z_unicast_link_serial_t *serial) { return &serial->_sock; }

#endif
