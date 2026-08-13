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

#include "zenoh-pico/link/multicast_link.h"

#include <string.h>

#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

z_result_t _z_multicast_link_get_dst_locator(const _z_multicast_link_t *link, const uint8_t *address,
                                             size_t address_len, char *buf, size_t buf_len) {
    const char *protocol = _z_multicast_link_get_protocol_name(link);
    size_t protocol_len = strlen(protocol);
    if (protocol_len < buf_len + 1) {
        memcpy(buf, protocol, protocol_len);
        buf[protocol_len] = LOCATOR_PROTOCOL_SEPARATOR;
        size_t remaining_len = buf_len - protocol_len - 1;

        uint16_t port = 0;
        if (address_len == sizeof(uint32_t) + sizeof(uint16_t)) {
            address_len = sizeof(uint32_t);
            port = ((uint16_t)address[4] << 8) | (uint16_t)address[5];
        } else if (address_len == 16 + sizeof(uint16_t)) {
            address_len = 16;
            port = ((uint16_t)address[16] << 8) | (uint16_t)address[17];
        } else {
            _Z_ERROR_RETURN(_Z_ERR_INVALID);
        }
        _Z_RETURN_IF_ERR(_z_ip_port_to_endpoint(address, address_len, port, buf + protocol_len + 1, remaining_len));
        return _Z_RES_OK;
    }
    return _Z_ERR_INVALID;
}

bool _z_multicast_link_send_wbuf(_z_multicast_link_t *link, const _z_wbuf_t *wbf) {
    bool link_is_streamed = _z_multicast_link_is_streamed(link);

    for (size_t i = 0; i < _z_wbuf_len_iosli(wbf); i++) {
        _z_slice_t bs = _z_iosli_to_bytes(_z_wbuf_get_iosli(wbf, i));
        size_t n = bs.len;
        while (n > 0) {
            size_t wb = n;
            if (!_z_multicast_link_write(link, bs.start, &wb) || wb > n) {
                return false;
            }

            if (link_is_streamed && wb != n) {
                return false;
            }
            n = n - wb;
            bs.start = bs.start + (bs.len - n);
        }
    }

    return true;
}

bool _z_multicast_link_recv_zbuf(_z_multicast_link_t *link, _z_zbuf_t *zbf, _z_link_address_t *addr_out) {
    _z_link_address_init(addr_out);
    size_t len = _z_zbuf_writable_space_left(zbf);
    if (_z_multicast_link_read(link, _z_zbuf_get_wptr(zbf), &len, addr_out)) {
        _z_zbuf_set_wpos(zbf, _z_zbuf_get_wpos(zbf) + len);
        return true;
    } else {
        // with current link implementation there is no way to distinguish between a read error and timeout
        return false;
    }
}
#endif
