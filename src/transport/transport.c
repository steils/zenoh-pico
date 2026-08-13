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

#include "zenoh-pico/transport/multicast/transport.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/transport/multicast/tx.h"
#include "zenoh-pico/transport/rx.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/tx.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/transport/unicast/tx.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/sleep.h"

z_result_t _z_transport_manager_create(_z_transport_manager_t *manager, _z_session_t *session) {
    memset(manager, 0, sizeof(_z_transport_manager_t));
#if Z_FEATURE_MULTI_THREAD == 1
    _Z_RETURN_IF_ERR(_z_mutex_init(&manager->_mutex));
#endif
    manager->_session = session;
#if Z_FEATURE_BATCHING == 0
#if Z_FEATURE_UNICAST_TRANSPORT == 0
    size_t buffer_size = Z_BATCH_MULTICAST_SIZE;
#elif Z_FEATURE_MULTICAST_TRANSPORT == 0
    size_t buffer_size = Z_BATCH_UNICAST_SIZE;
#else
    size_t buffer_size = Z_BATCH_MULTICAST_SIZE > Z_BATCH_UNICAST_SIZE ? Z_BATCH_MULTICAST_SIZE : Z_BATCH_UNICAST_SIZE;
#endif
    _Z_CLEAN_RETURN_IF_ERR(_z_wbuf_init(&manager->_tx_buffer, buffer_size, false), _z_transport_manager_clear(manager));
#else
    manager->_batching_enabled = false;
#endif
    manager->_lease_task = _z_fut_handle_null();
    manager->_read_task = _z_fut_handle_null();
    manager->_keep_alive_task = _z_fut_handle_null();
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
    manager->_connect_task = _z_fut_handle_null();
    _z_connect_info_init(&manager->_connect_info);
#endif
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _Z_CLEAN_RETURN_IF_ERR(_z_unicast_transport_manager_create(&manager->_unicast, manager),
                           _z_transport_manager_clear(manager));
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _Z_CLEAN_RETURN_IF_ERR(_z_multicast_transport_manager_create(&manager->_multicast, manager),
                           _z_transport_manager_clear(manager));
#endif
    return _Z_RES_OK;
}

z_result_t _z_transport_manager_spawn_tasks(_z_transport_manager_t *manager) {
    _z_fut_t lease_fut;
    lease_fut._fut_arg = manager;
    lease_fut._fut_fn = _z_transport_manager_lease_task_fn;
    lease_fut._destroy_fn = NULL;
    manager->_lease_task = _z_runtime_spawn(&manager->_session->_runtime, &lease_fut);
    if (_z_fut_handle_is_null(manager->_lease_task)) {
        _Z_ERROR("Failed to spawn transport manager lease task");
        return _Z_ERR_FAILED_TO_SPAWN_TASK;
    }
    _z_fut_t read_fut;
    read_fut._fut_arg = manager;
    read_fut._fut_fn = _z_transport_manager_read_task_fn;
    read_fut._destroy_fn = NULL;
    manager->_read_task = _z_runtime_spawn(&manager->_session->_runtime, &read_fut);
    if (_z_fut_handle_is_null(manager->_read_task)) {
        _Z_ERROR("Failed to spawn transport manager read task");
        return _Z_ERR_FAILED_TO_SPAWN_TASK;
    }
    _z_fut_t keep_alive_fut;
    keep_alive_fut._fut_arg = manager;
    keep_alive_fut._fut_fn = _z_transport_manager_keep_alive_task_fn;
    keep_alive_fut._destroy_fn = NULL;
    manager->_keep_alive_task = _z_runtime_spawn(&manager->_session->_runtime, &keep_alive_fut);
    if (_z_fut_handle_is_null(manager->_keep_alive_task)) {
        _Z_ERROR("Failed to spawn transport manager keep alive task with error");
        return _Z_ERR_FAILED_TO_SPAWN_TASK;
    }
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
    _z_fut_t connect_fut;
    connect_fut._fut_arg = manager;
    connect_fut._fut_fn = _z_transport_manager_connect_task_fn;
    connect_fut._destroy_fn = NULL;
    manager->_connect_task = _z_runtime_spawn(&manager->_session->_runtime, &connect_fut);
    if (_z_fut_handle_is_null(manager->_connect_task)) {
        _Z_ERROR("Failed to spawn transport manager connect task with error");
        return _Z_ERR_FAILED_TO_SPAWN_TASK;
    }
#endif
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _Z_RETURN_IF_ERR(_z_unicast_transport_manager_spawn_tasks(&manager->_unicast));
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _Z_RETURN_IF_ERR(_z_multicast_transport_manager_spawn_tasks(&manager->_multicast));
#endif
    return _Z_RES_OK;
}

void _z_transport_manager_close(_z_transport_manager_t *manager) {
    if (!_z_fut_handle_is_null(manager->_lease_task)) {
        _z_runtime_cancel_fut(&manager->_session->_runtime, &manager->_lease_task);
        manager->_lease_task = _z_fut_handle_null();
    }
    if (!_z_fut_handle_is_null(manager->_read_task)) {
        _z_runtime_cancel_fut(&manager->_session->_runtime, &manager->_read_task);
        manager->_read_task = _z_fut_handle_null();
    }
    if (!_z_fut_handle_is_null(manager->_keep_alive_task)) {
        _z_runtime_cancel_fut(&manager->_session->_runtime, &manager->_keep_alive_task);
        manager->_keep_alive_task = _z_fut_handle_null();
    }
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
    if (!_z_fut_handle_is_null(manager->_connect_task)) {
        _z_runtime_cancel_fut(&manager->_session->_runtime, &manager->_connect_task);
        manager->_connect_task = _z_fut_handle_null();
    }
#endif
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_unicast_transport_manager_close(&manager->_unicast);
    _z_unicast_transport_manager_clear(&manager->_unicast);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_transport_manager_close(&manager->_multicast);
    _z_multicast_transport_manager_clear(&manager->_multicast);
#endif
}

void _z_transport_manager_clear(_z_transport_manager_t *manager) {
    _z_transport_manager_close(manager);
#if Z_FEATURE_BATCHING == 0
    _z_wbuf_clear(&manager->_tx_buffer);
#endif
#if Z_FEATURE_MULTI_THREAD == 1
    _z_mutex_drop(&manager->_mutex);
#endif
}

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
z_result_t _z_transport_manager_add_listener(_z_transport_manager_t *manager, const _z_string_t *locator,
                                             _z_listen_listener_id_t locator_id) {
    _z_endpoint_t ep;
    _Z_RETURN_IF_ERR(_z_endpoint_from_string(&ep, locator));
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_link_t ml = _z_multicast_link_null();
    if (_z_multicast_link_create(&ml, &ep, &manager->_session->_config) == _Z_RES_OK) {
        _Z_CLEAN_RETURN_IF_ERR(_z_multicast_transport_manager_add_group(&manager->_multicast, &ml, locator_id),
                               _z_multicast_link_clear(&ml));
        _Z_INFO("Successfully added multicast listener for: "_Z_STRING_PRINT_FORMAT, _Z_STRING_PRINT_ARG(locator));
        return _Z_RES_OK;
    }
#endif
#if Z_FEATURE_UNICAST_TRANSPORT == 1 && Z_FEATURE_UNICAST_PEER == 1
    _z_unicast_listener_t ul = _z_unicast_listener_null();
    if (_z_unicast_listener_create(&ul, &ep, &manager->_session->_config) == _Z_RES_OK) {
        _Z_CLEAN_RETURN_IF_ERR(_z_unicast_transport_manager_add_listener(&manager->_unicast, &ul, locator_id),
                               _z_unicast_listener_clear(&ul));
        _Z_INFO("Successfully added unicast listener for: "_Z_STRING_PRINT_FORMAT, _Z_STRING_PRINT_ARG(locator));
        return _Z_RES_OK;
    }
#endif
    _z_endpoint_clear(&ep);
    _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN);
    return _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
}
#endif

#if Z_FEATURE_UNICAST_TRANSPORT == 1
z_result_t _z_transport_manager_connect_peer(_z_transport_manager_t *manager, const _z_string_t *locator,
                                             _z_connect_peer_id_t locator_id) {
    _z_endpoint_t ep;
    _Z_RETURN_IF_ERR(_z_endpoint_from_string(&ep, locator));
    _z_unicast_link_t ul = _z_unicast_link_null();
    if (_z_unicast_link_create(&ul, &ep, &manager->_session->_config) == _Z_RES_OK) {
        _Z_CLEAN_RETURN_IF_ERR(_z_unicast_transport_manager_connect_peer(&manager->_unicast, &ul, locator_id),
                               _z_unicast_link_clear(&ul));
        _Z_INFO("Successfully connected to: "_Z_STRING_PRINT_FORMAT, _Z_STRING_PRINT_ARG(locator));
        return _Z_RES_OK;
    }
    _z_endpoint_clear(&ep);
    _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN);
    return _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
}
#endif

size_t _z_transport_manager_get_peers_count(const _z_transport_manager_t *manager) {
    size_t count = 0;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    count += _z_unicast_transport_manager_get_peers_count(&manager->_unicast);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    count += _z_multicast_transport_manager_get_peers_count(&manager->_multicast);
#endif
    return count;
}

size_t _z_transport_manager_get_readers_count(const _z_transport_manager_t *manager) {
    size_t count = 0;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    count += _z_unicast_transport_manager_get_readers_count(&manager->_unicast);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    count += _z_multicast_transport_manager_get_readers_count(&manager->_multicast);
#endif
    return count;
}

_z_fut_fn_result_t _z_transport_manager_lease_task_fn(void *transport_manager, _z_executor_t *executor) {
    _ZP_UNUSED(executor);
    const z_clock_t *next_deadline = NULL;
    _z_transport_manager_t *manager = (_z_transport_manager_t *)transport_manager;
    if (_z_transport_manager_get_peers_count(manager) == 0) {
        return _z_fut_fn_result_suspend();
    }
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    next_deadline = _z_unicast_transport_manager_check_lease(&manager->_unicast);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    const z_clock_t *multicast_deadline = _z_multicast_transport_manager_check_lease(&manager->_multicast);
    if (next_deadline == NULL ||
        (multicast_deadline != NULL && zp_clock_compare(multicast_deadline, next_deadline) < 0)) {
        next_deadline = multicast_deadline;
    }
#endif
    if (next_deadline == NULL) {
        return _z_fut_fn_result_suspend();
    }
    return _z_fut_fn_result_wake_up_at(next_deadline);
}

_z_fut_fn_result_t _z_transport_manager_keep_alive_task_fn(void *transport_manager, _z_executor_t *executor) {
    _ZP_UNUSED(executor);
    _z_transport_manager_t *manager = (_z_transport_manager_t *)transport_manager;
    if (_z_transport_manager_get_peers_count(manager) == 0) {
        return _z_fut_fn_result_suspend();
    }

    z_result_t ret = _z_transport_manager_send_keep_alive(manager);
    return ret == _Z_RES_OK ? _z_fut_fn_result_wake_up_after(Z_TRANSPORT_LEASE / Z_TRANSPORT_LEASE_EXPIRE_FACTOR)
                            : _z_fut_fn_result_ready();
}

_z_fut_fn_result_t _z_transport_manager_read_task_fn(void *transport_manager, _z_executor_t *executor) {
    _ZP_UNUSED(executor);
    _z_transport_manager_t *manager = (_z_transport_manager_t *)transport_manager;
    if (_z_transport_manager_get_readers_count(manager) == 0) {
        return _z_fut_fn_result_suspend();
    }
    z_result_t ret = _z_transport_manager_read(manager);
#if Z_RUNTIME_IDLE_READ_TASK_SLEEP > 0
    if (ret == Z_NO_DATA) {
        return _z_fut_fn_result_wake_up_after(Z_RUNTIME_IDLE_READ_TASK_SLEEP);
    }
#else
    _ZP_UNUSED(ret);
#endif
    return _z_fut_fn_result_continue();
}

void _z_transport_manager_signal_closed_peer(_z_transport_manager_t *manager, _z_connect_peer_id_t locator_id) {
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
    if (locator_id < Z_MAX_NUM_UNICAST_PEERS) {
        _z_peer_mask_bitset_set_at(&manager->_connect_info.connected_locators, (size_t)locator_id, false);
    }
    // wake up reconnect task if it is suspended
    if (!_z_fut_handle_is_null(manager->_connect_task)) {
        _z_runtime_resume_suspended_or_wakeup_sleeping_fut(&manager->_session->_runtime, &manager->_connect_task);
    }
#else
    _ZP_UNUSED(locator_id);
    _ZP_UNUSED(manager);
#endif
}

void _z_transport_manager_signal_opened_peer(_z_transport_manager_t *manager, _z_connect_peer_id_t locator_id) {
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
    if (locator_id < Z_MAX_NUM_UNICAST_PEERS) {
        _z_peer_mask_bitset_set_at(&manager->_connect_info.connected_locators, locator_id, true);
    }
#else
    _ZP_UNUSED(locator_id);
#endif
    if (!_z_fut_handle_is_null(manager->_lease_task)) {
        _z_runtime_resume_suspended_or_wakeup_sleeping_fut(&manager->_session->_runtime, &manager->_lease_task);
    }
    if (!_z_fut_handle_is_null(manager->_keep_alive_task)) {
        _z_runtime_resume_suspended_fut(&manager->_session->_runtime, &manager->_keep_alive_task);
    }
    if (!_z_fut_handle_is_null(manager->_read_task)) {
        _z_runtime_resume_suspended_or_wakeup_sleeping_fut(&manager->_session->_runtime, &manager->_read_task);
    }
}

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_AUTO_RECONNECT == 1
_z_fut_fn_result_t _z_transport_manager_connect_task_fn(void *transport_manager, _z_executor_t *executor) {
    _ZP_UNUSED(executor);
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    uint32_t next_wake_up_time_ms = 0;
    _z_transport_manager_t *manager = (_z_transport_manager_t *)transport_manager;
    if (manager->_session->_mode == Z_WHATAMI_CLIENT &&
        (Z_FEATURE_AUTO_RECONNECT == 0 || _z_transport_manager_get_peers_count(manager) > 0)) {
        return _z_fut_fn_result_suspend();
    }
    if (_z_config_connect_vec_is_empty(&manager->_session->_config._connect)) {
#if Z_FEATURE_SCOUTING == 1
        if (manager->_session->_mode == Z_WHATAMI_CLIENT && manager->_session->_config._multicast_scouting._parsed) {
            z_result_t ret = _z_connect_first_via_scout(manager->_session);
            if (ret != _Z_RES_OK) {
                _Z_INFO("Failed to connect to any locator via scout: %d", ret);
            }
            _z_connect_info_increase_duration(&manager->_connect_info);
            next_wake_up_time_ms = manager->_connect_info.next_reconnect_attempt_in_ms;
        } else {
            return _z_fut_fn_result_suspend();
        }
#else
        return _z_fut_fn_result_suspend();
#endif
    } else {
        size_t i = manager->_connect_info.next_peer;
        for (; i < _z_config_connect_vec_size(&manager->_session->_config._connect); i++) {
            if (!*_z_peer_mask_bitset_const_at(&manager->_connect_info.connected_locators, i)) {
                const char *locator = *_z_config_connect_vec_at(&manager->_session->_config._connect, i);
                _z_string_view_t lv = _z_string_view_make_from_str(locator);
                z_result_t ret =
                    _z_transport_manager_connect_peer(manager, _z_string_view_deref(&lv), (_z_connect_peer_id_t)i);
                if (ret == _Z_RES_OK) {
                    _z_connect_info_reset_duration(&manager->_connect_info);
                } else {
                    _Z_ERROR("Failed to connect to %s: %d", locator, ret);
                }
                i++;
                break;
            }
        }
        manager->_connect_info.next_peer = i;
        if (manager->_connect_info.next_peer >= _z_config_connect_vec_size(&manager->_session->_config._connect)) {
            manager->_connect_info.next_peer = 0;
            _z_connect_info_increase_duration(&manager->_connect_info);
            next_wake_up_time_ms = manager->_connect_info.next_reconnect_attempt_in_ms;
        }
    }

    return next_wake_up_time_ms == 0 ? _z_fut_fn_result_continue()
                                     : _z_fut_fn_result_wake_up_after((unsigned long)next_wake_up_time_ms);
#else  // Z_FEATURE_UNICAST_TRANSPORT == 1
    _ZP_UNUSED(transport_manager);
    return _z_fut_fn_result_suspend();
#endif
}
#endif
