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

#include "zenoh-pico/protocol/definitions/transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/transport/multicast/connectivity.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/multicast/tx.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

z_result_t _z_multicast_transport_manager_create(_z_multicast_transport_manager_t *manager,
                                                 _z_transport_manager_t *parent) {
    memset(manager, 0, sizeof(_z_multicast_transport_manager_t));
    manager->_parent = parent;
    _z_multicast_transport_group_vec_init(&manager->_groups);
    _z_address_to_multicast_transport_peer_hmap_init(&manager->_peers);
    manager->_send_join_task_handle = _z_fut_handle_null();
    _z_multicast_lease_pqueue_init_with_ctx(&manager->_lease_pqueue, &manager->_peers);
    manager->_sn_res = _z_sn_modulo_mask(Z_SN_RESOLUTION);
    return _Z_RES_OK;
}

z_result_t _z_multicast_transport_manager_spawn_tasks(_z_multicast_transport_manager_t *manager) {
    z_result_t ret = _Z_RES_OK;
    if (!_z_multicast_transport_group_vec_is_empty(&manager->_groups)) {
        _z_fut_t fut;
        fut._fut_arg = manager;
        fut._fut_fn = _z_multicast_transport_manager_send_join_task_fn;
        fut._destroy_fn = NULL;
        manager->_send_join_task_handle = _z_runtime_spawn(&manager->_parent->_session->_runtime, &fut);
        if (_z_fut_handle_is_null(manager->_send_join_task_handle)) {
            ret = _Z_ERR_FAILED_TO_SPAWN_TASK;
            _Z_ERROR("Failed to spawn multicast transport send join task with error %d", ret);
            return ret;
        }
    }
    return ret;
}

z_result_t _z_multicast_transport_manager_add_group(_z_multicast_transport_manager_t *manager,
                                                    _z_multicast_link_t *link, _z_listen_listener_id_t locator_id) {
    _z_multicast_transport_group_t group;
    memset(&group, 0, sizeof(_z_multicast_transport_group_t));
    group._parent = manager;
    group._link = *link;
    uint16_t mtu = _z_multicast_link_get_mtu(link);
    mtu = mtu < Z_BATCH_MULTICAST_SIZE ? mtu : Z_BATCH_MULTICAST_SIZE;
    z_random_fill(&group._sn_tx._reliable, sizeof(group._sn_tx._reliable));
    group._sn_tx._reliable &= _z_sn_modulo_mask(Z_SN_RESOLUTION);
    group._sn_tx._best_effort = group._sn_tx._reliable;
    group._transmitted = false;
    group._locator_id = locator_id;
#if Z_FEATURE_BATCHING == 1
    _Z_RETURN_IF_ERR(_z_wbuf_init(&group._tx_buffer, mtu, false));
    _Z_CLEAN_RETURN_IF_ERR(_z_zbuf_init(&group._rx_buffer, mtu), _z_wbuf_clear(&group._tx_buffer));
#else
    _Z_RETURN_IF_ERR(_z_zbuf_init(&group._rx_buffer, mtu));
#endif

    if (!_z_multicast_transport_group_vec_push_back(&manager->_groups, &group)) {
        _z_multicast_transport_group_clear(&group);
        _Z_ERROR_RETURN(_Z_ERR_SYSTEM_OUT_OF_MEMORY);
    }
    if (_z_multicast_transport_group_vec_size(&manager->_groups) == 1 &&
        !_z_fut_handle_is_null(manager->_send_join_task_handle)) {
        _Z_RETURN_IF_ERR(_z_runtime_resume_suspended_or_wakeup_sleeping_fut(&manager->_parent->_session->_runtime,
                                                                            &manager->_send_join_task_handle));
    }
    return _Z_RES_OK;
}
void _z_multicast_transport_manager_close(_z_multicast_transport_manager_t *manager) {
    _z_transport_message_t msg = _z_t_msg_make_close(_Z_CLOSE_REASON_GENERIC, false);
    for (size_t i = 0; i < _z_multicast_transport_group_vec_size(&manager->_groups); i++) {
        _z_multicast_transport_manager_send_t_msg_to_group(manager, &msg, i);
    }
}

void _z_multicast_transport_manager_clear(_z_multicast_transport_manager_t *manager) {
    if (!_z_fut_handle_is_null(manager->_send_join_task_handle)) {
        _z_runtime_cancel_fut(&manager->_parent->_session->_runtime, &manager->_send_join_task_handle);
        manager->_send_join_task_handle = _z_fut_handle_null();
    }
    _z_address_to_multicast_transport_peer_hmap_destroy(&manager->_peers);
    _z_multicast_transport_group_vec_destroy(&manager->_groups);
    _z_multicast_lease_pqueue_destroy(&manager->_lease_pqueue);
}

z_result_t _z_multicast_transport_manager_close_peer(
    _z_multicast_transport_manager_t *manager, _z_address_to_multicast_transport_peer_hmap_iter_t peer_id,
    const _z_close_reason_t *opt_reason, _z_address_to_multicast_transport_peer_hmap_iter_t *opt_next_peer_id) {
    _ZP_UNUSED(opt_reason);
    _z_multicast_transport_peer_t *peer =
        &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
    _ZP_UNUSED(peer);
    _Z_INFO("Closing multicast peer " _Z_ID_PRINT_FORMAT " on group %zu", _Z_ID_PRINT_ARGS(&peer->_remote_zid),
            (size_t)peer->_multicast_group);
    _z_multicast_transport_manager_report_disconnected_event(manager, peer_id);
    _ZP_REMOVE_ONE(_z_multicast_lease_pqueue, &manager->_lease_pqueue, *_ == peer_id);
    _Z_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent));
    _z_address_to_multicast_transport_peer_hmap_remove_at(&manager->_peers, peer_id, NULL, opt_next_peer_id);
    _z_transport_manager_unlock(manager->_parent);
    return _Z_RES_OK;
}

_z_fut_fn_result_t _z_multicast_transport_manager_send_join_task_fn(void *multicast_transport_manager,
                                                                    _z_executor_t *executor) {
    _ZP_UNUSED(executor);
    _z_multicast_transport_manager_t *manager = (_z_multicast_transport_manager_t *)multicast_transport_manager;

    if (_z_multicast_transport_group_vec_size(&manager->_groups) == 0) {
        return _z_fut_fn_result_suspend();
    }

    z_result_t ret = _z_multicast_transport_manager_send_join(manager);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("Failed to send JOIN message to multicast groups (err: %d)", ret);
        return _z_fut_fn_result_ready();
    }
    return _z_fut_fn_result_wake_up_after(Z_JOIN_INTERVAL);
}

const z_clock_t *_z_multicast_transport_manager_check_lease(_z_multicast_transport_manager_t *manager) {
    z_clock_t now = z_clock_now();

    while (!_z_multicast_lease_pqueue_is_empty(&manager->_lease_pqueue)) {
        _z_address_to_multicast_transport_peer_hmap_iter_t peer_id =
            *_z_multicast_lease_pqueue_peek(&manager->_lease_pqueue);
        _z_multicast_transport_peer_t *peer =
            &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
        if (zp_clock_compare(&now, &peer->_lease_deadline) <= 0) {
            break;
        }
        _z_close_reason_t reason = _Z_CLOSE_REASON_EXPIRED;
        if (!peer->_received) {
            _Z_INFO("Lease expired for multicast peer %zu, group %zu", (size_t)peer_id, (size_t)peer->_multicast_group);
            z_result_t ret = _z_multicast_transport_manager_close_peer(manager, peer_id, &reason, NULL);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to remove expired multicast peer %zu from group %zu", (size_t)peer_id,
                         (size_t)peer->_multicast_group);
                return NULL;
            }
        } else {
            _z_multicast_lease_pqueue_pop(&manager->_lease_pqueue, NULL);
            peer->_received = false;
            peer->_lease_deadline = now;
            z_clock_advance_ms(&peer->_lease_deadline, (unsigned long)peer->_lease_duration_ms);
            _z_multicast_lease_pqueue_push(&manager->_lease_pqueue, &peer_id);
        }
    }

    if (_z_multicast_lease_pqueue_is_empty(&manager->_lease_pqueue)) {
        return NULL;
    }
    _z_address_to_multicast_transport_peer_hmap_iter_t next = *_z_multicast_lease_pqueue_peek(&manager->_lease_pqueue);
    return &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, next)->val._lease_deadline;
}

#endif  // Z_FEATURE_MULTICAST_TRANSPORT == 1
