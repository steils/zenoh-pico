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

#include "zenoh-pico/transport/connectivity.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/transport/multicast/connectivity.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/unicast/connectivity.h"
#include "zenoh-pico/transport/unicast/transport.h"

void _z_transport_manager_fetch_zid(const _z_transport_manager_t *manager, _z_closure_zid_t *callback,
                                    z_what_t filter) {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_unicast_transport_manager_fetch_zid(&manager->_unicast, callback, filter);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_transport_manager_fetch_zid(&manager->_multicast, callback, filter);
#endif
}

bool _z_transport_manager_has_peer_with_zid(const _z_transport_manager_t *manager, const _z_id_t *zid) {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    if (_z_unicast_transport_manager_has_peer_with_zid(&manager->_unicast, zid)) {
        return true;
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    if (_z_multicast_transport_manager_has_peer_with_zid(&manager->_multicast, zid)) {
        return true;
    }
#endif
    return false;
}

bool _z_transport_manager_has_multicast(const _z_transport_manager_t *manager) {
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    return !_z_multicast_transport_group_vec_is_empty(&manager->_multicast._groups);
#else
    _ZP_UNUSED(manager);
    return false;
#endif
}

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
_z_string_view_t _z_transport_manager_get_listen_locator(const _z_transport_manager_t *manager,
                                                         _z_listen_listener_id_t listener_id) {
    const char *config_locator = *_z_config_listen_vec_at(&manager->_session->_config._listen, listener_id);
    size_t locator_len = strlen(config_locator);
    const char *metadata_start = memchr(config_locator, LOCATOR_METADATA_SEPARATOR, locator_len);
    if (metadata_start != NULL) {
        return _z_string_view_make(config_locator, (size_t)(metadata_start - config_locator));
    }
    const char *cfg_start = memchr(config_locator, ENDPOINT_CONFIG_SEPARATOR, locator_len);
    if (cfg_start != NULL) {
        return _z_string_view_make(config_locator, (size_t)(cfg_start - config_locator));
    }
    return _z_string_view_make(config_locator, locator_len);
}
#endif

#if Z_FEATURE_CONNECTIVITY == 1
void _z_transport_manager_fetch_transports(const _z_transport_manager_t *manager, z_closure_transport_callback_t call,
                                           void *arg) {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_unicast_transport_manager_fetch_transports(&manager->_unicast, call, arg);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_transport_manager_fetch_transports(&manager->_multicast, call, arg);
#endif
}
void _z_transport_manager_fetch_links(const _z_transport_manager_t *manager, z_closure_link_callback_t call, void *arg,
                                      const _z_info_transport_t *opt_transport_filter) {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_unicast_transport_manager_fetch_links(&manager->_unicast, call, arg, opt_transport_filter);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_transport_manager_fetch_links(&manager->_multicast, call, arg, opt_transport_filter);
#endif
}
#endif
