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
#ifndef INCLUDE_ZENOH_PICO_TRANSPORT_UNICAST_CONNECTIVITY_H
#define INCLUDE_ZENOH_PICO_TRANSPORT_UNICAST_CONNECTIVITY_H

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/api/types.h"
#include "zenoh-pico/transport/unicast/transport.h"

#ifdef __cplusplus
extern "C" {
#endif
#if Z_FEATURE_UNICAST_TRANSPORT == 1
void _z_unicast_transport_manager_report_connected_event(_z_unicast_transport_manager_t *manager,
                                                         _z_unicast_transport_peer_hset_iter_t iter);
void _z_unicast_transport_manager_report_disconnected_event(_z_unicast_transport_manager_t *manager,
                                                            _z_unicast_transport_peer_hset_iter_t iter);

void _z_unicast_transport_manager_fetch_zid(const _z_unicast_transport_manager_t *manager, _z_closure_zid_t *callback,
                                            z_what_t filter);
bool _z_unicast_transport_manager_has_peer_with_zid(const _z_unicast_transport_manager_t *manager, const _z_id_t *zid);

#if Z_FEATURE_CONNECTIVITY == 1
void _z_unicast_transport_manager_fetch_transports(const _z_unicast_transport_manager_t *manager,
                                                   z_closure_transport_callback_t call, void *arg);
void _z_unicast_transport_manager_fetch_links(const _z_unicast_transport_manager_t *manager,
                                              z_closure_link_callback_t call, void *arg,
                                              const _z_info_transport_t *opt_transport_filter);
#endif
#ifdef __cplusplus
}
#endif
#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
#endif  // INCLUDE_ZENOH_PICO_TRANSPORT_UNICAST_CONNECTIVITY_H
