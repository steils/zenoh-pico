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
//

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/transport/peers.h"

#ifndef ZENOH_PICO_TRANSPORT_DESTINATION_FILTER_H
#define ZENOH_PICO_TRANSPORT_DESTINATION_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _z_transport_type_t {
    _Z_TRANSPORT_TYPE_UNICAST = 0x01,
    _Z_TRANSPORT_TYPE_MULTICAST = 0x02,
    _Z_TRANSPORT_TYPE_ALL = (_Z_TRANSPORT_TYPE_UNICAST | _Z_TRANSPORT_TYPE_MULTICAST),
} _z_transport_type_t;

#define _ZP_VARIANT_TEMPLATE_NAME _z_destination_filter
#define _ZP_VARIANT_TEMPLATE_1_TYPE uint32_t
#define _ZP_VARIANT_TEMPLATE_1_NAME single
#define _ZP_VARIANT_TEMPLATE_2_TYPE _z_peer_mask_bitset_t
#define _ZP_VARIANT_TEMPLATE_2_NAME mask
#define _ZP_VARIANT_TEMPLATE_3_TYPE z_what_t
#define _ZP_VARIANT_TEMPLATE_3_NAME what
#include "zenoh-pico/collections/variant_template.h"

static inline _z_destination_filter_t _z_destination_filter_all(void) {
    return _z_destination_filter_from_what(&(z_what_t){Z_WHAT_ROUTER_PEER_CLIENT});
}

static inline _z_destination_filter_t _z_destination_filter_from_single_val(size_t peer_id) {
    uint32_t peer_id_u32 = (uint32_t)peer_id;
    return _z_destination_filter_from_single(&peer_id_u32);
}

#ifdef __cplusplus
}
#endif

#endif  // ZENOH_PICO_TRANSPORT_DESTINATION_FILTER_H
