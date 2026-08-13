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

#ifndef ZENOH_PICO_UNICAST_LISTENER_H
#define ZENOH_PICO_UNICAST_LISTENER_H

#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/transport/peers.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _z_unicast_transport_manager_t _z_unicast_transport_manager_t;
#if Z_FEATURE_UNICAST_TRANSPORT == 1 && Z_FEATURE_UNICAST_PEER == 1

typedef struct _z_unicast_transport_listener_t {
    _z_unicast_listener_t _listener;
    _z_unicast_transport_manager_t *_manager;  // non-owning pointer to the transport manager that owns this listener
    _z_listen_listener_id_t _locator_id;       // idx of locator in config._listen
} _z_unicast_transport_listener_t;

z_result_t _z_unicast_transport_listener_listen(_z_unicast_transport_listener_t *listener);
void _z_unicast_transport_listener_clear(_z_unicast_transport_listener_t *listener);

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_UNICAST_LISTENER_H */
