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

#ifndef INCLUDE_ZENOH_PICO_TRANSPORT_CONNECTIVITY_H
#define INCLUDE_ZENOH_PICO_TRANSPORT_CONNECTIVITY_H
#include "zenoh-pico/api/types.h"
#include "zenoh-pico/transport/transport.h"
#ifdef __cplusplus
extern "C" {
#endif

// Transport manager should be locked before calling this function outside of background thread
void _z_transport_manager_fetch_zid(const _z_transport_manager_t *manager, _z_closure_zid_t *callback, z_what_t filter);
bool _z_transport_manager_has_multicast(const _z_transport_manager_t *manager);

// Transport manager should be locked before calling this function outside of background thread
bool _z_transport_manager_has_peer_with_zid(const _z_transport_manager_t *manager, const _z_id_t *zid);

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
_z_string_view_t _z_transport_manager_get_listen_locator(const _z_transport_manager_t *manager,
                                                         _z_listen_listener_id_t listener_id);
#endif
#if Z_FEATURE_CONNECTIVITY == 1
// Transport manager should be locked before calling this function outside of background thread
void _z_transport_manager_fetch_transports(const _z_transport_manager_t *manager, z_closure_transport_callback_t call,
                                           void *arg);
// Transport manager should be locked before calling this function outside of background thread
void _z_transport_manager_fetch_links(const _z_transport_manager_t *manager, z_closure_link_callback_t call, void *arg,
                                      const _z_info_transport_t *opt_transport_filter);
#endif
#ifdef __cplusplus
}
#endif

#endif  // INCLUDE_ZENOH_PICO_TRANSPORT_CONNECTIVITY_H
