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

#ifndef ZENOH_PICO_MULTICAST_RX_H
#define ZENOH_PICO_MULTICAST_RX_H

#include <stddef.h>

#include "zenoh-pico/link/address.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/rx.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
z_result_t _z_multicast_transport_manager_read(_z_multicast_transport_manager_t *manager,
                                               const _z_ready_links_mask_bitset_t *ready_links);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_MULTICAST_RX_H */
