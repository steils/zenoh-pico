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

// #include "zenoh-pico/transport/link/rx.h"

#include <stddef.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_RAWETH_TRANSPORT == 1

z_result_t _z_raweth_process_rx_buffer(_z_zbuf_t *zbf) {
    uint8_t *buff = _z_zbuf_get_rptr(zbf);
    size_t readable_len = _z_zbuf_readable_len(zbf);
    if (readable_len < sizeof(_zp_eth_header_t)) {
        return _Z_ERR_TRANSPORT_RX_FAILED;
    }
    // Check if header has vlan
    bool has_vlan = false;
    _zp_eth_header_t *header = (_zp_eth_header_t *)buff;
    if (header->ethtype == _ZP_ETH_TYPE_VLAN) {
        has_vlan = true;
    }
    // Check validity
    if (has_vlan && (readable_len < sizeof(_zp_eth_vlan_header_t))) {
        return _Z_ERR_TRANSPORT_RX_FAILED;
    }
    size_t data_length = 0;
    if (has_vlan) {
        _zp_eth_vlan_header_t *vlan_header = (_zp_eth_vlan_header_t *)buff;
        // Retrieve data length
        data_length = _z_raweth_ntohs(vlan_header->data_length);
        if (readable_len < (data_length + sizeof(_zp_eth_vlan_header_t))) {
            // Invalid data_length
            return _Z_ERR_TRANSPORT_RX_FAILED;
        }
        // Skip header
        _z_zbuf_set_wpos(zbf, _z_zbuf_get_rpos(zbf) + sizeof(_zp_eth_vlan_header_t) + data_length);
        _z_zbuf_set_rpos(zbf, _z_zbuf_get_rpos(zbf) + sizeof(_zp_eth_vlan_header_t));
    } else {
        header = (_zp_eth_header_t *)buff;
        // Retrieve data length
        data_length = _z_raweth_ntohs(header->data_length);
        if (readable_len < (data_length + sizeof(_zp_eth_header_t))) {
            // Invalid data_length
            return _Z_ERR_TRANSPORT_RX_FAILED;
        }
        // Skip header
        _z_zbuf_set_wpos(zbf, _z_zbuf_get_rpos(zbf) + sizeof(_zp_eth_header_t) + data_length);
        _z_zbuf_set_rpos(zbf, _z_zbuf_get_rpos(zbf) + sizeof(_zp_eth_header_t));
    }
    return _Z_RES_OK;
}

#endif  // Z_FEATURE_RAWETH_TRANSPORT == 1
