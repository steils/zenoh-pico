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

// #include "zenoh-pico/transport/link/tx.h"

#include "zenoh-pico/transport/tx.h"

#include <string.h>

#include "zenoh-pico/api/primitives.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/protocol/codec/core.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/session/keyexpr.h"
#include "zenoh-pico/transport/multicast/group.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1 && Z_FEATURE_RAWETH_TRANSPORT == 1

static int _zp_raweth_find_map_entry(const _z_keyexpr_t *keyexpr, _z_multicast_link_raweth_t *link) {
    for (size_t i = 0; i < _zp_raweth_mapping_array_len(&link->_mapping); i++) {
        // Find matching keyexpr
        const _zp_raweth_mapping_entry_t *entry = _zp_raweth_mapping_array_get(&link->_mapping, i);
        _z_keyexpr_view_t entry_ke = _z_keyexpr_view_from_string(&entry->_keyexpr);
        if (!_z_keyexpr_intersects(keyexpr, _z_keyexpr_view_deref(&entry_ke))) {
            continue;
        }
        return (int)i;
    }
    return -1;
}

static void _zp_raweth_set_socket(const _z_keyexpr_t *keyexpr, _z_multicast_link_raweth_t *link) {
    if (keyexpr == NULL) {
        // Store default value into socket
        const _zp_raweth_mapping_entry_t *entry = _zp_raweth_mapping_array_get(&link->_mapping, 0);
        // Flawfinder: ignore [CWE-120] - fixed-size MAC copy, both operands are _ZP_MAC_ADDR_LENGTH bytes.
        memcpy(link->_dmac, entry->_dmac, _ZP_MAC_ADDR_LENGTH);
        link->_has_vlan = entry->_has_vlan;
        if (link->_has_vlan) {
            link->_vlan = entry->_vlan;
        }
    } else {
        // Find config entry (linear)
        int idx = _zp_raweth_find_map_entry(keyexpr, link);
        // Key not found case
        if (idx < 0) {
            idx = 0;  // Set to default entry
            _Z_DEBUG(
                "Key '"_Z_KEYEXPR_PRINT_FORMAT
                "' wasn't found in config mapping, sending to default address",
                _Z_KEYEXPR_PRINT_ARG(keyexpr));
        }
        // Store data into socket
        const _zp_raweth_mapping_entry_t *entry = _zp_raweth_mapping_array_get(&link->_mapping, (size_t)idx);
        // Flawfinder: ignore [CWE-120] - fixed-size MAC copy, both operands are _ZP_MAC_ADDR_LENGTH bytes.
        memcpy(link->_dmac, entry->_dmac, _ZP_MAC_ADDR_LENGTH);
        link->_has_vlan = entry->_has_vlan;
        if (link->_has_vlan) {
            link->_vlan = entry->_vlan;
        }
    }
}

void _z_raweth_wbf_and_link_prepare(_z_wbuf_t *wbf, _z_multicast_link_raweth_t *zl, const _z_network_message_t *n_msg) {
    const _z_wireexpr_t *wireexpr = NULL;
    if (n_msg != NULL) {
        switch (n_msg->_tag) {
            case _Z_N_PUSH:
                wireexpr = &n_msg->_body._push._key;
                break;
            case _Z_N_REQUEST:
                wireexpr = &n_msg->_body._request._key;
                break;
            case _Z_N_RESPONSE:
                wireexpr = &n_msg->_body._response._key;
                break;
            case _Z_N_RESPONSE_FINAL:
            case _Z_N_DECLARE:
            default:
                break;
        }
    }
    if (wireexpr == NULL) {
        _zp_raweth_set_socket(NULL, zl);
    } else {
        _z_keyexpr_view_t keyexpr = _z_keyexpr_view_from_string_view(&wireexpr->_suffix);
        _zp_raweth_set_socket(_z_keyexpr_view_deref(&keyexpr), zl);
    }
    // Reserve eth header in buffer
    if (zl->_has_vlan) {
        _z_wbuf_set_wpos(wbf, sizeof(_zp_eth_vlan_header_t));
    } else {
        _z_wbuf_set_wpos(wbf, sizeof(_zp_eth_header_t));
    }
}

void _z_raweth_wbuf_and_link_finalize(_z_wbuf_t *wbf, _z_multicast_link_raweth_t *zl) {
    // Save and reset buffer position
    size_t wpos = _z_wbuf_len(wbf);
    _z_wbuf_set_wpos(wbf, 0);
    // Write eth header in buffer
    if (zl->_has_vlan) {
        _zp_eth_vlan_header_t header;
        // Set header
        memset(&header, 0, sizeof(header));
        // Flawfinder: ignore [CWE-120] - fixed-size MAC copy, both operands are _ZP_MAC_ADDR_LENGTH bytes.
        memcpy(header.dmac, zl->_dmac, _ZP_MAC_ADDR_LENGTH);
        // Flawfinder: ignore [CWE-120] - fixed-size MAC copy, both operands are _ZP_MAC_ADDR_LENGTH bytes.
        memcpy(header.smac, zl->_smac, _ZP_MAC_ADDR_LENGTH);
        header.vlan_type = _ZP_ETH_TYPE_VLAN;
        header.tag = zl->_vlan;
        header.ethtype = zl->_ethtype;
        header.data_length = _z_raweth_htons((uint16_t)(wpos - sizeof(header)));
        // Write header
        _z_wbuf_write_bytes(wbf, (uint8_t *)&header, 0, sizeof(header));
    } else {
        _zp_eth_header_t header;
        // Set header
        // Flawfinder: ignore [CWE-120] - fixed-size MAC copy, both operands are _ZP_MAC_ADDR_LENGTH bytes.
        memcpy(header.dmac, zl->_dmac, _ZP_MAC_ADDR_LENGTH);
        // Flawfinder: ignore [CWE-120] - fixed-size MAC copy, both operands are _ZP_MAC_ADDR_LENGTH bytes.
        memcpy(header.smac, zl->_smac, _ZP_MAC_ADDR_LENGTH);
        header.ethtype = zl->_ethtype;
        header.data_length = _z_raweth_htons((uint16_t)(wpos - sizeof(header)));
        // Write header
        _z_wbuf_write_bytes(wbf, (uint8_t *)&header, 0, sizeof(header));
    }
    // Restore wpos
    _z_wbuf_set_wpos(wbf, wpos);
}
#endif  // Z_FEATURE_MULTICAST_TRANSPORT == 1 && Z_FEATURE_RAWETH_TRANSPORT == 1
