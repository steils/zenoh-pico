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

#ifndef INCLUDE_ZENOH_PICO_TRANSPORT_TRANSPORT_H
#define INCLUDE_ZENOH_PICO_TRANSPORT_TRANSPORT_H

#include <assert.h>
#include <stdint.h>

#include "zenoh-pico/collections/element.h"
#include "zenoh-pico/collections/refcount.h"
#include "zenoh-pico/collections/slice.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/peers.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/utils/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _z_session_t _z_session_t;
typedef struct _z_transport_manager_t {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_unicast_transport_manager_t _unicast;
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_transport_manager_t _multicast;
#endif
    _z_session_t *_session;  // non-owning pointer to the session that owns this transport manager
#if Z_FEATURE_MULTI_THREAD == 1
    _z_mutex_t _mutex;
#endif
#if Z_FEATURE_BATCHING == 0
    _z_wbuf_t _tx_buffer;  // a common buffer used for outgoing messages
#else
    bool _batching_enabled;  // whether batching is enabled or not
#endif
    _z_fut_handle_t _read_task;
    _z_fut_handle_t _lease_task;
    _z_fut_handle_t _keep_alive_task;
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
    _z_fut_handle_t _connect_task;
    _z_connect_info_t _connect_info;
#endif

    _z_peer_mask_bitset_t
        _connected_endpoints_mask;  // a bitmask corresponding to connected endpoints from config._connect
} _z_transport_manager_t;

static inline z_result_t _z_transport_manager_lock(const _z_transport_manager_t *manager) {
#if Z_FEATURE_MULTI_THREAD == 1
    return _z_mutex_lock((_z_mutex_t *)&(manager->_mutex));
#else
    _ZP_UNUSED(manager);
    return _Z_RES_OK;
#endif
}

static inline z_result_t _z_transport_manager_try_lock(const _z_transport_manager_t *manager) {
#if Z_FEATURE_MULTI_THREAD == 1
    return _z_mutex_try_lock((_z_mutex_t *)&(manager->_mutex));
#else
    _ZP_UNUSED(manager);
    return _Z_RES_OK;
#endif
}

static inline z_result_t _z_transport_manager_unlock(const _z_transport_manager_t *manager) {
#if Z_FEATURE_MULTI_THREAD == 1
    return _z_mutex_unlock((_z_mutex_t *)&(manager->_mutex));
#else
    _ZP_UNUSED(manager);
    return _Z_RES_OK;
#endif
}

z_result_t _z_transport_manager_create(_z_transport_manager_t *manager, _z_session_t *session);
z_result_t _z_transport_manager_spawn_tasks(_z_transport_manager_t *manager);
// The transport manager should be locked before calling this function.
void _z_transport_manager_close(_z_transport_manager_t *manager);
void _z_transport_manager_clear(_z_transport_manager_t *manager);

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
// add either unicast or multicast listener depending on the locator type
z_result_t _z_transport_manager_add_listener(_z_transport_manager_t *manager, const _z_string_t *locator,
                                             _z_listen_listener_id_t locator_id);
#endif
#if Z_FEATURE_UNICAST_TRANSPORT == 1
// connect unicast peer
z_result_t _z_transport_manager_connect_peer(_z_transport_manager_t *manager, const _z_string_t *locator,
                                             _z_connect_peer_id_t locator_id);
#endif

// The transport manager should be locked before calling this function.
size_t _z_transport_manager_get_peers_count(const _z_transport_manager_t *manager);
// The transport manager should be locked before calling this function.
size_t _z_transport_manager_get_readers_count(const _z_transport_manager_t *manager);

_z_fut_fn_result_t _z_transport_manager_read_task_fn(void *transport_manager, _z_executor_t *executor);
_z_fut_fn_result_t _z_transport_manager_lease_task_fn(void *transport_manager, _z_executor_t *executor);
_z_fut_fn_result_t _z_transport_manager_keep_alive_task_fn(void *transport_manager, _z_executor_t *executor);
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
_z_fut_fn_result_t _z_transport_manager_connect_task_fn(void *transport_manager, _z_executor_t *executor);
#endif

void _z_transport_manager_signal_opened_peer(_z_transport_manager_t *manager, _z_connect_peer_id_t locator_id);
void _z_transport_manager_signal_closed_peer(_z_transport_manager_t *manager, _z_connect_peer_id_t locator_id);

#if Z_FEATURE_CONNECTIVITY == 1
typedef struct {
    _z_id_t _remote_zid;
    z_whatami_t _remote_whatami;
    _z_string_view_t _link_src;
    _z_string_view_t _link_dst;
    _z_string_view_t _mcast_group;
} _z_connectivity_peer_event_data_t;
#endif

typedef struct {
    _z_sn_t _initial_sn_tx;
    uint8_t _seq_num_res;
} _z_transport_multicast_establish_param_t;

#ifdef __cplusplus
}
#endif
#endif /* INCLUDE_ZENOH_PICO_TRANSPORT_TRANSPORT_H */
