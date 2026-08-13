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

#include "zenoh-pico/transport/admin_space.h"

#include "zenoh-pico/transport/multicast/admin_space.h"
#include "zenoh-pico/transport/unicast/admin_space.h"

#if Z_FEATURE_ADMIN_SPACE == 1
z_result_t _z_transport_manager_encode_locators_json(const _z_transport_manager_t *manager, _z_json_encoder_t *je) {
    _Z_RETURN_IF_ERR(_z_json_encoder_start_array(je));
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _Z_RETURN_IF_ERR(_z_unicast_transport_manager_encode_locators_json(&manager->_unicast, je));
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _Z_RETURN_IF_ERR(_z_multicast_transport_manager_encode_locators_json(&manager->_multicast, je));
#endif
    return _z_json_encoder_end_array(je);
}

z_result_t _z_transport_manager_encode_transports_json(const _z_transport_manager_t *manager, _z_json_encoder_t *je) {
    _Z_RETURN_IF_ERR(_z_json_encoder_start_array(je));
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _Z_RETURN_IF_ERR(_z_unicast_transport_manager_encode_transports_json(&manager->_unicast, je));
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _Z_RETURN_IF_ERR(_z_multicast_transport_manager_encode_transports_json(&manager->_multicast, je));
#endif
    return _z_json_encoder_end_array(je);
}
#endif
