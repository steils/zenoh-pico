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

#ifndef ZENOH_PICO_MULTICAST_TX_H
#define ZENOH_PICO_MULTICAST_TX_H

#include "zenoh-pico/protocol/definitions/network.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/transport/destination_filter.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/transport.h"

#ifdef __cplusplus
extern "C" {
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
z_result_t _z_multicast_transport_manager_send_n_msg(_z_multicast_transport_manager_t *manager,
                                                     const _z_network_message_t *msg, z_reliability_t reliability,
                                                     _z_destination_filter_t dest_filter);
z_result_t _z_multicast_transport_manager_send_n_msg_to_group(_z_multicast_transport_manager_t *manager,
                                                              const _z_network_message_t *msg,
                                                              z_reliability_t reliability, size_t mcast_group_id);
z_result_t _z_multicast_transport_manager_send_t_msg_to_group(_z_multicast_transport_manager_t *manager,
                                                              const _z_transport_message_t *msg, size_t mcast_group_id);
#if Z_FEATURE_BATCHING == 1
z_result_t _z_multicast_transport_manager_send_n_batch(_z_multicast_transport_manager_t *manager);
#endif

z_result_t _z_multicast_transport_manager_send_join(_z_multicast_transport_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif  // Z_FEATURE_MULTICAST_TRANSPORT == 1

#endif /* ZENOH_PICO_MULTICAST_TX_H */
