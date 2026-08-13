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

#ifndef INCLUDE_ZENOH_PICO_TRANSPORT_MULTICAST_ADMIN_SPACE_H
#define INCLUDE_ZENOH_PICO_TRANSPORT_MULTICAST_ADMIN_SPACE_H

#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/utils/json_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
#if Z_FEATURE_ADMIN_SPACE == 1
z_result_t _z_multicast_transport_manager_encode_locators_json(const _z_multicast_transport_manager_t *manager,
                                                               _z_json_encoder_t *je);
z_result_t _z_multicast_transport_manager_encode_transports_json(const _z_multicast_transport_manager_t *manager,
                                                                 _z_json_encoder_t *je);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_ZENOH_PICO_TRANSPORT_MULTICAST_ADMIN_SPACE_H
