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

#include "zenoh-pico/link/unicast_link.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1
z_result_t _z_unicast_link_get_locators(const _z_unicast_link_t *link, char *local, size_t local_len, char *remote,
                                        size_t remote_len) {
    const char *protocol = _z_unicast_link_get_protocol_name(link);
    size_t protocol_len = strlen(protocol);
    if (protocol_len < local_len + 1 && protocol_len < remote_len + 1) {
        memcpy(remote, protocol, protocol_len);
        memcpy(local, protocol, protocol_len);
        remote[protocol_len] = LOCATOR_PROTOCOL_SEPARATOR;
        local[protocol_len] = LOCATOR_PROTOCOL_SEPARATOR;
        size_t remote_remaining_len = remote_len - protocol_len - 1;
        size_t local_remaining_len = local_len - protocol_len - 1;
        size_t offset = protocol_len + 1;
        return _z_unicast_link_get_endpoints(link, local + offset, local_remaining_len, remote + offset,
                                             remote_remaining_len);
    }
    return _Z_ERR_INVALID;
}

bool _z_unicast_link_recv_zbuf(_z_unicast_link_t *link, _z_zbuf_t *zbf) {
    size_t len = _z_zbuf_writable_space_left(zbf);
    if (_z_unicast_link_read(link, _z_zbuf_get_wptr(zbf), &len)) {
        _z_zbuf_set_wpos(zbf, _z_zbuf_get_wpos(zbf) + len);
        return true;
    } else {
        // with current link implementation there is no way to distinguish between a read error and timeout
        return false;
    }
}

bool _z_unicast_link_send_wbuf(_z_unicast_link_t *link, const _z_wbuf_t *wbf) {
    bool link_is_streamed = _z_unicast_link_is_streamed(link);

    for (size_t i = 0; i < _z_wbuf_len_iosli(wbf); i++) {
        _z_slice_t bs = _z_iosli_to_bytes(_z_wbuf_get_iosli(wbf, i));
        size_t n = bs.len;
        while (n > 0) {
            size_t wb = n;
            if (!_z_unicast_link_write(link, bs.start, &wb) || wb > n) {
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
#endif
