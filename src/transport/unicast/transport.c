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
#include "zenoh-pico/transport/transport.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/link/transport/socket.h"
#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/system/common/platform.h"
#include "zenoh-pico/transport/connectivity.h"
#include "zenoh-pico/transport/unicast/connectivity.h"
#include "zenoh-pico/transport/unicast/rx.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/transport/unicast/tx.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1

z_result_t _z_unicast_transport_manager_create(_z_unicast_transport_manager_t *manager,
                                               _z_transport_manager_t *parent) {
    memset(manager, 0, sizeof(_z_unicast_transport_manager_t));
#if Z_FEATURE_UNICAST_PEER == 1
    _z_listen_data_init(&manager->_listen_data);
    manager->_listen_task = _z_fut_handle_null();
#endif
    manager->_parent = parent;
    _z_unicast_transport_peer_hmap_init(&manager->_peers);
    _z_unicast_lease_pqueue_init_with_ctx(&manager->_lease_pqueue, &manager->_peers);
    manager->_rx_buffer = _z_zbuf_null();
    return _Z_RES_OK;
}

// The caller holds the transport-manager lock, so the counter and map lookup
// form one atomic key-allocation operation.
static _z_unicast_peer_key_t _z_unicast_transport_manager_next_peer_key(_z_unicast_transport_manager_t *manager) {
    _z_unicast_peer_key_t key;
    do {
        key = _z_unicast_peer_key_numeric(manager->_next_numeric_peer_id++);
    } while (_z_unicast_transport_peer_hmap_get_iter(&manager->_peers, &key) !=
             _z_unicast_transport_peer_hmap_end(&manager->_peers));
    return key;
}

size_t _z_unicast_transport_manager_get_pending_count(const _z_unicast_transport_manager_t *manager) {
    size_t count = 0;
    for (_z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_hmap_begin(&manager->_peers);
         id != _z_unicast_transport_peer_hmap_end(&manager->_peers);
         id = _z_unicast_transport_peer_hmap_iter_next(&manager->_peers, id)) {
        count += _z_unicast_peer_slot_state_is_pending(manager->_peer_states[id]) ? 1 : 0;
    }
    return count;
}

bool _z_unicast_transport_manager_has_pending_locator(const _z_unicast_transport_manager_t *manager,
                                                      _z_connect_peer_id_t locator_id) {
    for (_z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_hmap_begin(&manager->_peers);
         id != _z_unicast_transport_peer_hmap_end(&manager->_peers);
         id = _z_unicast_transport_peer_hmap_iter_next(&manager->_peers, id)) {
        const _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_const_at(&manager->_peers, id);
        if (_z_unicast_handshake_state_is_open(manager->_peer_states[id]) && peer->_locator_id == locator_id) {
            return true;
        }
    }
    return false;
}

bool _z_unicast_transport_manager_has_pending_open(const _z_unicast_transport_manager_t *manager) {
    for (_z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_hmap_begin(&manager->_peers);
         id != _z_unicast_transport_peer_hmap_end(&manager->_peers);
         id = _z_unicast_transport_peer_hmap_iter_next(&manager->_peers, id)) {
        if (_z_unicast_handshake_state_is_open(manager->_peer_states[id])) {
            return true;
        }
    }
    return false;
}

void _z_unicast_transport_manager_abort_pending(_z_unicast_transport_manager_t *manager, _z_unicast_peer_slot_id_t id,
                                                z_result_t reason) {
    if ((size_t)id >= Z_MAX_NUM_UNICAST_PEERS) {
        return;
    }
    z_result_t ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        return;
    }
    if (!_z_unicast_peer_slot_state_is_pending(manager->_peer_states[id])) {
        _z_transport_manager_unlock(manager->_parent);
        return;
    }
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_at(&manager->_peers, id);
    _z_connect_peer_id_t locator_id = peer->_locator_id;
    bool outbound = _z_unicast_handshake_state_is_open(manager->_peer_states[id]);
    _Z_INFO("Aborting pending unicast handshake %zu (err: %d)", (size_t)id, reason);
    _ZP_REMOVE_ONE(_z_unicast_lease_pqueue, &manager->_lease_pqueue, *_ == id);
    manager->_peer_states[id] = _Z_UNICAST_SLOT_FREE;
    _z_unicast_transport_peer_hmap_remove_at(&manager->_peers, id, NULL, NULL);
    _z_transport_manager_unlock(manager->_parent);
    if (outbound) {
        _z_transport_manager_signal_closed_peer(manager->_parent, locator_id);
    }
}

static z_result_t _z_unicast_transport_manager_start_pending(_z_unicast_transport_manager_t *manager,
                                                             _z_unicast_link_t *link, _z_connect_peer_id_t locator_id,
                                                             _z_unicast_handshake_role_t role) {
    if (!_z_unicast_link_is_streamed(link) || _z_unicast_link_get_socket(link) == NULL) {
        return _Z_ERR_INVALID;
    }
    z_result_t ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    bool has_capacity = _z_unicast_transport_peer_hmap_size(&manager->_peers) < Z_MAX_NUM_UNICAST_PEERS;
    _z_transport_manager_unlock(manager->_parent);
    if (!has_capacity) {
        return _Z_ERR_TRANSPORT_NO_SPACE;
    }

    _z_unicast_transport_peer_t peer = {
        ._link = _z_unicast_link_null(),
        ._rx_buffer = _z_zbuf_null(),
        ._locator_id = locator_id,
    };
    _Z_RETURN_IF_ERR(_z_zbuf_init(&peer._rx_buffer, Z_BATCH_UNICAST_SIZE));

    peer._lease_deadline = z_clock_now();
    z_clock_advance_ms(&peer._lease_deadline, role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN ? Z_TRANSPORT_CONNECT_TIMEOUT
                                                                                     : Z_TRANSPORT_ACCEPT_TIMEOUT);
    _z_session_t *session = manager->_parent->_session;
    _z_unicast_peer_slot_state_t state = _Z_UNICAST_SLOT_FREE;
    if (role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN) {
        _z_transport_message_t output;
        uint16_t batch_size = _z_unicast_link_get_mtu(link);
        batch_size = batch_size < Z_BATCH_UNICAST_SIZE ? batch_size : Z_BATCH_UNICAST_SIZE;
        _z_unicast_handshake_start_open(&peer, &state, batch_size, &session->_local_zid, session->_mode, &output);
        ret = _z_unicast_link_send_t_msg(link, &output);
        if (ret != _Z_RES_OK) {
            _z_zbuf_clear(&peer._rx_buffer);
            return ret;
        }
    } else {
        _z_unicast_handshake_start_accept(&peer, &state, session->_mode);
    }
    ret = _z_socket_set_blocking(_z_unicast_link_get_socket(link), false);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&peer._rx_buffer);
        return ret;
    }
    ret = _z_transport_manager_lock(manager->_parent);
    _z_unicast_peer_slot_id_t slot_id = _z_unicast_transport_peer_hmap_end(&manager->_peers);
    if (ret == _Z_RES_OK) {
        if (_z_unicast_transport_peer_hmap_size(&manager->_peers) >= Z_MAX_NUM_UNICAST_PEERS) {
            ret = _Z_ERR_TRANSPORT_NO_SPACE;
        } else {
            peer._link = *link;
            _z_unicast_peer_key_t key = _z_unicast_transport_manager_next_peer_key(manager);
            slot_id = _z_unicast_transport_peer_hmap_insert(&manager->_peers, &key, &peer);
            if (slot_id == _z_unicast_transport_peer_hmap_end(&manager->_peers)) {
                ret = _Z_ERR_TRANSPORT_NO_SPACE;
            } else {
                *link = _z_unicast_link_null();
                manager->_peer_states[slot_id] = state;
            }
        }
        _z_transport_manager_unlock(manager->_parent);
    }
    if (ret != _Z_RES_OK) {
        (void)_z_socket_set_blocking(_z_unicast_link_get_socket(link), true);
        _z_zbuf_clear(&peer._rx_buffer);
        return ret;
    }
    (void)_z_unicast_lease_pqueue_push(&manager->_lease_pqueue, &slot_id);
    if (!_z_fut_handle_is_null(manager->_parent->_lease_task)) {
        (void)_z_runtime_resume_suspended_or_wakeup_sleeping_fut(&session->_runtime, &manager->_parent->_lease_task);
    }
    if (_z_fut_handle_is_null(manager->_parent->_read_task)) {
        return _Z_RES_OK;
    }
    (void)_z_runtime_resume_suspended_or_wakeup_sleeping_fut(&session->_runtime, &manager->_parent->_read_task);
    return _Z_RES_OK;
}

static z_result_t _z_unicast_transport_manager_start_pending_open(_z_unicast_transport_manager_t *manager,
                                                                  _z_unicast_link_t *link,
                                                                  _z_connect_peer_id_t locator_id) {
    if (_z_unicast_transport_manager_has_pending_locator(manager, locator_id)) {
        return Z_RETRY_LATER;
    }
    return _z_unicast_transport_manager_start_pending(manager, link, locator_id, _Z_UNICAST_HANDSHAKE_ROLE_OPEN);
}

static z_result_t _z_unicast_transport_manager_start_pending_accept(_z_unicast_transport_manager_t *manager,
                                                                    _z_unicast_link_t *link) {
    return _z_unicast_transport_manager_start_pending(manager, link, _ZP_CONNECT_PEER_ID_FROM_LISTEN,
                                                      _Z_UNICAST_HANDSHAKE_ROLE_ACCEPT);
}

z_result_t _z_unicast_transport_manager_spawn_tasks(_z_unicast_transport_manager_t *manager) {
    z_result_t ret = _Z_RES_OK;
    _ZP_UNUSED(manager);
#if Z_FEATURE_UNICAST_PEER == 1
    if (!_z_unicast_transport_listener_vec_is_empty(&manager->_listeners)) {
        _z_fut_t fut;
        fut._fut_arg = manager;
        fut._fut_fn = _zp_unicast_transport_listen_task_fn;
        fut._destroy_fn = NULL;
        manager->_listen_task = _z_runtime_spawn(&manager->_parent->_session->_runtime, &fut);
        if (_z_fut_handle_is_null(manager->_listen_task)) {
            ret = _Z_ERR_FAILED_TO_SPAWN_TASK;
            _Z_ERROR("Failed to spawn unicast transport listen task with error %d", ret);
            return ret;
        }
    }
#endif
    return ret;
}
void _z_unicast_transport_manager_close(_z_unicast_transport_manager_t *manager) {
    _z_transport_message_t msg = _z_t_msg_make_close(_Z_CLOSE_REASON_GENERIC, false);
    for (_z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_established_begin(manager);
         id != _z_unicast_transport_peer_hmap_end(&manager->_peers);
         id = _z_unicast_transport_peer_established_iter_next(manager, id)) {
        _z_unicast_transport_manager_send_t_msg_to_peer(manager, &msg, id);
    }
}

void _z_unicast_transport_manager_clear(_z_unicast_transport_manager_t *manager) {
#if Z_FEATURE_UNICAST_PEER == 1
    if (!_z_fut_handle_is_null(manager->_listen_task)) {
        _z_runtime_cancel_fut(&manager->_parent->_session->_runtime, &manager->_listen_task);
        manager->_listen_task = _z_fut_handle_null();
    }
#endif
    // Accepted TLS peers reference listener-owned TLS configuration and must be destroyed first.
    _z_unicast_transport_peer_hmap_destroy(&manager->_peers);
#if Z_FEATURE_UNICAST_PEER == 1
    _z_unicast_transport_listener_vec_destroy(&manager->_listeners);
#endif
    _z_unicast_lease_pqueue_destroy(&manager->_lease_pqueue);
    _z_zbuf_clear(&manager->_rx_buffer);
}

static z_result_t _z_unicast_transport_manager_validate_peer(const _z_unicast_transport_manager_t *manager,
                                                             const _z_id_t *zid) {
    if (_z_id_eq(&manager->_parent->_session->_local_zid, zid)) {
        return _Z_ATTEMPT_SELF_CONNECT;
    }
    return _z_transport_manager_has_peer_with_zid(manager->_parent, zid) ? _Z_ALREADY_CONNECTED : _Z_RES_OK;
}

static _z_close_reason_t _z_unicast_transport_rejection_reason(z_result_t ret) {
    switch (ret) {
        case _Z_ATTEMPT_SELF_CONNECT:
            return _Z_CLOSE_REASON_CONNECTION_TO_SELF;
        case _Z_ALREADY_CONNECTED:
            return _Z_CLOSE_REASON_MAX_LINKS;
        case _Z_CAPACITY_LIMIT_REACHED:
        case _Z_ERR_TRANSPORT_NO_SPACE:
            return _Z_CLOSE_REASON_MAX_TRANSPORTS;
        default:
            return _Z_CLOSE_REASON_GENERIC;
    }
}

static void _z_unicast_transport_reject_peer(_z_unicast_link_t *link, const _z_id_t *zid, z_result_t ret) {
    if (ret > _Z_RES_OK) {
        _Z_WARN("Rejecting peer " _Z_ID_PRINT_FORMAT " connection (%d)", _Z_ID_PRINT_ARGS(zid), ret);
    } else {
        _Z_ERROR("Failed to add peer " _Z_ID_PRINT_FORMAT " to transport manager: %d", _Z_ID_PRINT_ARGS(zid), ret);
    }
    _z_transport_message_t close = _z_t_msg_make_close(_z_unicast_transport_rejection_reason(ret), true);
    (void)_z_unicast_link_send_t_msg(link, &close);
}

static void _z_unicast_transport_peer_clear_runtime(_z_unicast_transport_peer_t *peer) {
#if Z_FEATURE_BATCHING == 1
    _z_wbuf_clear(&peer->_tx_buffer);
#endif
#if Z_FEATURE_FRAGMENTATION == 1
    _z_dbuf_clear(&peer->_dbuf);
#endif
    _ZP_UNUSED(peer);
}

static z_result_t _z_unicast_transport_peer_init_runtime(_z_unicast_transport_peer_t *peer) {
    peer->_received = true;
    peer->_transmitted = false;
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_patch = peer->_patch < _Z_CURRENT_PATCH ? peer->_patch : _Z_CURRENT_PATCH;
    _Z_RETURN_IF_ERR(_z_dbuf_init(&peer->_dbuf));
#endif
#if Z_FEATURE_BATCHING == 1
    z_result_t ret = _z_wbuf_init(&peer->_tx_buffer, peer->_batch_size, false);
    if (ret != _Z_RES_OK) {
#if Z_FEATURE_FRAGMENTATION == 1
        _z_dbuf_clear(&peer->_dbuf);
#endif
        return ret;
    }
#endif
    return _Z_RES_OK;
}

static void _z_unicast_transport_peer_set_lease_deadline(_z_unicast_transport_peer_t *peer) {
    peer->_lease_deadline = z_clock_now();
    z_clock_advance_ms(&peer->_lease_deadline, (unsigned long)peer->_lease_duration_ms);
}

static void _z_unicast_transport_manager_report_added_peer(_z_unicast_transport_manager_t *manager,
                                                           _z_unicast_peer_slot_id_t id) {
    const _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_const_at(&manager->_peers, id);
    _Z_INFO("Added new unicast peer " _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&peer->_remote_zid));
    (void)_z_unicast_lease_pqueue_push(&manager->_lease_pqueue, &id);
    _z_transport_manager_signal_opened_peer(manager->_parent, peer->_locator_id);
    _z_unicast_transport_manager_report_connected_event(manager, id);
}

z_result_t _z_unicast_transport_manager_establish_pending(_z_unicast_transport_manager_t *manager,
                                                          _z_unicast_peer_slot_id_t id) {
    if ((size_t)id >= Z_MAX_NUM_UNICAST_PEERS || !_z_unicast_peer_slot_state_is_pending(manager->_peer_states[id])) {
        return _Z_ERR_INVALID;
    }
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_at(&manager->_peers, id);
    z_result_t ret = _z_unicast_transport_peer_init_runtime(peer);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        _z_unicast_transport_peer_clear_runtime(peer);
        return ret;
    }
    if (!_z_unicast_peer_slot_state_is_pending(manager->_peer_states[id])) {
        ret = _Z_ERR_INVALID;
    } else {
        ret = _z_unicast_transport_manager_validate_peer(manager, &peer->_remote_zid);
        if (ret == _Z_RES_OK) {
            _ZP_REMOVE_ONE(_z_unicast_lease_pqueue, &manager->_lease_pqueue, *_ == id);
            _z_unicast_transport_peer_set_lease_deadline(peer);
            manager->_peer_states[id] = _Z_UNICAST_SLOT_ESTABLISHED;
        }
    }
    _z_transport_manager_unlock(manager->_parent);
    if (ret != _Z_RES_OK) {
        _z_unicast_transport_reject_peer(&peer->_link, &peer->_remote_zid, ret);
        _z_unicast_transport_peer_clear_runtime(peer);
        return ret;
    }
    _z_unicast_transport_manager_report_added_peer(manager, id);
    return _Z_RES_OK;
}

static z_result_t _z_unicast_transport_manager_add_peer_inner(_z_unicast_transport_manager_t *manager,
                                                              _z_unicast_transport_peer_t *peer,
                                                              _z_unicast_link_t *link, _z_connect_peer_id_t locator_id,
                                                              _z_zbuf_t *opt_rx_leftover) {
    _Z_RETURN_IF_ERR(_z_unicast_transport_peer_init_runtime(peer));
    _z_zbuf_t rx_buffer = _z_zbuf_null();
    z_result_t ret = _Z_RES_OK;
    if (_z_unicast_link_is_streamed(link)) {
        ret = _z_zbuf_init(&rx_buffer, peer->_batch_size);
        if (ret == _Z_RES_OK && opt_rx_leftover != NULL && _z_zbuf_readable_len(opt_rx_leftover) > 0) {
            _z_zbuf_copy_bytes(&rx_buffer, opt_rx_leftover);
        }
    } else if (_z_zbuf_capacity(&manager->_rx_buffer) == 0) {
        ret = _z_zbuf_init(&manager->_rx_buffer, Z_BATCH_UNICAST_SIZE);
    }
    if (ret != _Z_RES_OK) {
        _z_unicast_transport_peer_clear_runtime(peer);
        return ret;
    }
    ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&rx_buffer);
        _z_unicast_transport_peer_clear_runtime(peer);
        return ret;
    }
    _z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_hmap_end(&manager->_peers);
    ret = _z_unicast_transport_manager_validate_peer(manager, &peer->_remote_zid);
    if (ret == _Z_RES_OK && _z_unicast_transport_peer_hmap_size(&manager->_peers) >= Z_MAX_NUM_UNICAST_PEERS) {
        ret = _Z_CAPACITY_LIMIT_REACHED;
    }
    if (ret == _Z_RES_OK) {
        _z_unicast_transport_peer_set_lease_deadline(peer);
        peer->_link = *link;
        peer->_rx_buffer = rx_buffer;
        peer->_locator_id = locator_id;
        _z_unicast_peer_key_t key = _z_unicast_transport_manager_next_peer_key(manager);
        id = _z_unicast_transport_peer_hmap_insert(&manager->_peers, &key, peer);
        if (id == _z_unicast_transport_peer_hmap_end(&manager->_peers)) {
            ret = _Z_CAPACITY_LIMIT_REACHED;
        } else {
            manager->_peer_states[id] = _Z_UNICAST_SLOT_ESTABLISHED;
            *link = _z_unicast_link_null();
        }
    }
    _z_transport_manager_unlock(manager->_parent);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&rx_buffer);
        _z_unicast_transport_peer_clear_runtime(peer);
        return ret;
    }
    _z_unicast_transport_manager_report_added_peer(manager, id);
    return _Z_RES_OK;
}

z_result_t _z_unicast_transport_manager_add_peer(_z_unicast_transport_manager_t *manager,
                                                 _z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                                 _z_connect_peer_id_t locator_id, _z_zbuf_t *opt_rx_leftover) {
    z_result_t ret = _z_unicast_transport_manager_add_peer_inner(manager, peer, link, locator_id, opt_rx_leftover);
    if (ret != _Z_RES_OK) {
        _z_unicast_transport_reject_peer(link, &peer->_remote_zid, ret);
    }
    return ret;
}

#if Z_FEATURE_UNICAST_PEER == 1
z_result_t _z_unicast_transport_manager_add_listener(_z_unicast_transport_manager_t *manager,
                                                     _z_unicast_listener_t *listener,
                                                     _z_listen_listener_id_t locator_id) {
    _z_unicast_transport_listener_t tl;
    tl._manager = manager;
    tl._listener = *listener;
    tl._locator_id = locator_id;

    if (!_z_unicast_transport_listener_vec_push_back(&manager->_listeners, &tl)) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }

    return Z_OK;
}
#endif

void _z_unicast_handshake_start_open(_z_unicast_transport_peer_t *peer, _z_unicast_peer_slot_state_t *state,
                                     uint16_t batch_size, const _z_id_t *local_zid, z_whatami_t mode,
                                     _z_transport_message_t *output) {
    *state = _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK;
    *output = _z_t_msg_make_init_syn(mode, *local_zid, batch_size);
    peer->_sn_res = _z_sn_max(output->_body._init._seq_num_res);
    peer->_batch_size = output->_body._init._batch_size;
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_patch = output->_body._init._patch;
#endif
}

void _z_unicast_handshake_start_accept(_z_unicast_transport_peer_t *peer, _z_unicast_peer_slot_state_t *state,
                                       z_whatami_t mode) {
    assert(mode == Z_WHATAMI_PEER);
    *state = _Z_UNICAST_HS_ACCEPT_WAIT_INIT;
    _ZP_UNUSED(peer);
}

static z_result_t _z_unicast_handshake_open_init_ack(_z_unicast_transport_peer_t *peer,
                                                     _z_unicast_peer_slot_state_t *state,
                                                     const _z_transport_message_t *input,
                                                     _z_transport_message_t *output, bool *has_output) {
    if ((_Z_MID(input->_header) != _Z_MID_T_INIT) || !_Z_HAS_FLAG(input->_header, _Z_FLAG_T_INIT_A)) {
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }

    const _z_t_msg_init_t *init_ack = &input->_body._init;
    z_result_t ret = _Z_RES_OK;
    _z_zint_t sn_res = _z_sn_max(init_ack->_seq_num_res);
    if (sn_res <= peer->_sn_res) {
        peer->_sn_res = sn_res;
    } else {
        ret = _Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION;
    }
    if (init_ack->_req_id_res > Z_REQ_RESOLUTION) {
        ret = _Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION;
    }
    if (init_ack->_batch_size <= peer->_batch_size) {
        peer->_batch_size = init_ack->_batch_size;
    } else {
        ret = _Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    if (init_ack->_patch <= peer->_patch) {
        peer->_patch = init_ack->_patch;
    } else {
        ret = _Z_ERR_GENERIC;
    }
#endif
    if (ret != _Z_RES_OK) {
        return ret;
    }

    peer->_remote_whatami = init_ack->_whatami;
    peer->_remote_zid = init_ack->_zid;
    z_random_fill(&peer->_sn_tx._reliable, sizeof(peer->_sn_tx._reliable));
    peer->_sn_tx._reliable &= peer->_sn_res;
    peer->_sn_tx._best_effort = peer->_sn_tx._reliable;

    *output =
        _z_t_msg_make_open_syn(Z_TRANSPORT_LEASE, peer->_sn_tx._reliable, _z_slice_view_deref(&init_ack->_cookie));
    *has_output = true;
    *state = _Z_UNICAST_HS_OPEN_WAIT_OPEN_ACK;
    return _Z_RES_OK;
}

static z_result_t _z_unicast_handshake_open_open_ack(_z_unicast_transport_peer_t *peer,
                                                     const _z_transport_message_t *input, bool *complete) {
    if ((_Z_MID(input->_header) != _Z_MID_T_OPEN) || !_Z_HAS_FLAG(input->_header, _Z_FLAG_T_OPEN_A)) {
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_sn_rx._reliable = input->_body._open._initial_sn;
    peer->_sn_rx._best_effort = input->_body._open._initial_sn;
#endif
    peer->_lease_duration_ms =
        (uint32_t)(input->_body._open._lease < Z_TRANSPORT_LEASE ? input->_body._open._lease : Z_TRANSPORT_LEASE);
    *complete = true;
    return _Z_RES_OK;
}

static z_result_t _z_unicast_handshake_accept_init(_z_unicast_transport_peer_t *peer,
                                                   _z_unicast_peer_slot_state_t *state, const _z_id_t *local_zid,
                                                   z_whatami_t local_whatami, const _z_transport_message_t *input,
                                                   _z_transport_message_t *output, bool *has_output) {
    if ((_Z_MID(input->_header) != _Z_MID_T_INIT) || _Z_HAS_FLAG(input->_header, _Z_FLAG_T_INIT_A)) {
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }

    _z_slice_t cookie = _z_slice_null();
    *output = _z_t_msg_make_init_ack(local_whatami, *local_zid, &cookie);
    if (input->_body._init._seq_num_res < output->_body._init._seq_num_res) {
        output->_body._init._seq_num_res = input->_body._init._seq_num_res;
    }
    if (input->_body._init._req_id_res < output->_body._init._req_id_res) {
        output->_body._init._req_id_res = input->_body._init._req_id_res;
    }
    if (input->_body._init._batch_size < output->_body._init._batch_size) {
        output->_body._init._batch_size = input->_body._init._batch_size;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    if (input->_body._init._patch < output->_body._init._patch) {
        output->_body._init._patch = input->_body._init._patch;
    }
#endif

    peer->_sn_res = _z_sn_max(output->_body._init._seq_num_res);
    peer->_batch_size = output->_body._init._batch_size;
    peer->_remote_zid = input->_body._init._zid;
    peer->_remote_whatami = input->_body._init._whatami;
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_patch = output->_body._init._patch;
#endif
    *state = _Z_UNICAST_HS_ACCEPT_WAIT_OPEN;
    *has_output = true;
    return _Z_RES_OK;
}

static z_result_t _z_unicast_handshake_accept_open(_z_unicast_transport_peer_t *peer,
                                                   const _z_transport_message_t *input, _z_transport_message_t *output,
                                                   bool *has_output, bool *complete) {
    if ((_Z_MID(input->_header) != _Z_MID_T_OPEN) || _Z_HAS_FLAG(input->_header, _Z_FLAG_T_OPEN_A)) {
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_sn_rx._reliable = input->_body._open._initial_sn;
    peer->_sn_rx._best_effort = input->_body._open._initial_sn;
#endif
    peer->_lease_duration_ms =
        (uint32_t)(input->_body._open._lease < Z_TRANSPORT_LEASE ? input->_body._open._lease : Z_TRANSPORT_LEASE);
    *output = _z_t_msg_make_open_ack(Z_TRANSPORT_LEASE, peer->_sn_tx._reliable);
    *has_output = true;
    *complete = true;
    return _Z_RES_OK;
}

z_result_t _z_unicast_handshake_on_input(_z_unicast_transport_peer_t *peer, _z_unicast_peer_slot_state_t *state,
                                         const _z_id_t *local_zid, z_whatami_t local_whatami,
                                         const _z_transport_message_t *input, _z_transport_message_t *output,
                                         bool *has_output, bool *complete) {
    *has_output = false;
    *complete = false;
    switch (*state) {
        case _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK:
            return _z_unicast_handshake_open_init_ack(peer, state, input, output, has_output);
        case _Z_UNICAST_HS_OPEN_WAIT_OPEN_ACK:
            return _z_unicast_handshake_open_open_ack(peer, input, complete);
        case _Z_UNICAST_HS_ACCEPT_WAIT_INIT:
            return _z_unicast_handshake_accept_init(peer, state, local_zid, local_whatami, input, output, has_output);
        case _Z_UNICAST_HS_ACCEPT_WAIT_OPEN:
            return _z_unicast_handshake_accept_open(peer, input, output, has_output, complete);
        default:
            return _Z_ERR_INVALID;
    }
}

static void _z_unicast_handshake_log_message(const char *action, const _z_transport_message_t *message) {
    if (_Z_MID(message->_header) == _Z_MID_T_INIT) {
        _Z_DEBUG("%s Z_INIT(%s)", action, _Z_HAS_FLAG(message->_header, _Z_FLAG_T_INIT_A) ? "Ack" : "Syn");
    } else if (_Z_MID(message->_header) == _Z_MID_T_OPEN) {
        _Z_DEBUG("%s Z_OPEN(%s)", action, _Z_HAS_FLAG(message->_header, _Z_FLAG_T_OPEN_A) ? "Ack" : "Syn");
    }
}

z_result_t _z_unicast_handshake_drive_sync(_z_unicast_transport_peer_t *peer, void *io_context,
                                           _z_unicast_handshake_send_f send_f, _z_unicast_handshake_recv_f recv_f,
                                           uint16_t batch_size, const _z_id_t *local_zid, z_whatami_t mode,
                                           _z_unicast_handshake_role_t role, _z_zbuf_t *opt_rx_leftover) {
    if (role != _Z_UNICAST_HANDSHAKE_ROLE_OPEN && role != _Z_UNICAST_HANDSHAKE_ROLE_ACCEPT) {
        return _Z_ERR_INVALID;
    }
    if (opt_rx_leftover != NULL) {
        *opt_rx_leftover = _z_zbuf_null();
    }
    bool open = role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN;
    _z_unicast_peer_slot_state_t state = _Z_UNICAST_SLOT_FREE;
    z_clock_t deadline = z_clock_now();
    z_clock_advance_ms(&deadline, open ? Z_TRANSPORT_CONNECT_TIMEOUT : Z_TRANSPORT_ACCEPT_TIMEOUT);

    _z_transport_message_t output = {0};
    bool has_output = open;
    bool complete = false;
    if (open) {
        _z_unicast_handshake_start_open(peer, &state, batch_size, local_zid, mode, &output);
    } else {
        _z_unicast_handshake_start_accept(peer, &state, mode);
    }

    _z_zbuf_t zbuf;
    z_result_t ret = _z_zbuf_init(&zbuf, Z_BATCH_UNICAST_SIZE);
    if (ret != _Z_RES_OK) {
        return ret;
    }

    while (true) {
        if (has_output) {
            _z_unicast_handshake_log_message("Sending", &output);
            ret = send_f(io_context, &output);
            if (ret != _Z_RES_OK) {
                break;
            }
            has_output = false;
        }
        if (complete) {
            break;
        }

        _z_unicast_peer_slot_state_t previous_state = state;
        _z_transport_message_t input = {0};
        ret = recv_f(io_context, &input, &zbuf, deadline);
        if (ret != _Z_RES_OK) {
            break;
        }
        _z_unicast_handshake_log_message("Received", &input);
        ret = _z_unicast_handshake_on_input(peer, &state, local_zid, mode, &input, &output, &has_output, &complete);
        if (ret != _Z_RES_OK) {
            break;
        }

        if (previous_state == _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK) {
            // OpenSyn borrows the InitAck cookie, so send it before replacing the receive buffer.
            _z_unicast_handshake_log_message("Sending", &output);
            ret = send_f(io_context, &output);
            if (ret != _Z_RES_OK) {
                break;
            }
            has_output = false;
            _z_zbuf_clear(&zbuf);
            ret = _z_zbuf_init(&zbuf, peer->_batch_size);
            if (ret != _Z_RES_OK) {
                break;
            }
        } else if (previous_state == _Z_UNICAST_HS_ACCEPT_WAIT_INIT) {
            _z_zbuf_reset(&zbuf);
        }
    }

    if (ret == _Z_RES_OK && open && opt_rx_leftover != NULL && _z_zbuf_readable_len(&zbuf) > 0) {
        *opt_rx_leftover = zbuf;
    } else {
        _z_zbuf_clear(&zbuf);
    }
    return ret;
}

static z_result_t _z_unicast_handshake_link_send(void *context, const _z_transport_message_t *message) {
    return _z_unicast_link_send_t_msg((_z_unicast_link_t *)context, message);
}

static z_result_t _z_unicast_handshake_link_recv(void *context, _z_transport_message_t *message, _z_zbuf_t *zbuf,
                                                 z_clock_t deadline) {
    return _z_unicast_link_recv_t_msg((_z_unicast_link_t *)context, message, zbuf, deadline);
}

static z_result_t _z_unicast_handshake_open(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                            const _z_id_t *local_zid, z_whatami_t mode, _z_zbuf_t *rx_leftover) {
    *rx_leftover = _z_zbuf_null();
    uint16_t batch_size = _z_unicast_link_get_mtu(link);
    batch_size = batch_size < Z_BATCH_UNICAST_SIZE ? batch_size : Z_BATCH_UNICAST_SIZE;
    return _z_unicast_handshake_drive_sync(peer, link, _z_unicast_handshake_link_send, _z_unicast_handshake_link_recv,
                                           batch_size, local_zid, mode, _Z_UNICAST_HANDSHAKE_ROLE_OPEN, rx_leftover);
}

z_result_t _z_unicast_handshake_listen(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                       const _z_id_t *local_zid, z_whatami_t mode) {
    return _z_unicast_handshake_drive_sync(peer, link, _z_unicast_handshake_link_send, _z_unicast_handshake_link_recv,
                                           Z_BATCH_UNICAST_SIZE, local_zid, mode, _Z_UNICAST_HANDSHAKE_ROLE_ACCEPT,
                                           NULL);
}

static z_result_t _z_unicast_transport_manager_accept_peer_sync(_z_unicast_transport_manager_t *manager,
                                                                _z_unicast_link_t *link) {
    _z_session_t *session = manager->_parent->_session;
    _z_unicast_transport_peer_t peer = {0};
    _Z_RETURN_IF_ERR(_z_unicast_handshake_listen(&peer, link, &session->_local_zid, session->_mode));

    return _z_unicast_transport_manager_add_peer(manager, &peer, link, _ZP_CONNECT_PEER_ID_FROM_LISTEN, NULL);
}

static bool _z_unicast_link_prepare_async_handshake(_z_unicast_link_t *link) {
#if !defined(ZP_PLATFORM_SOCKET_POSIX) && !defined(ZP_PLATFORM_SOCKET_WINDOWS) &&            \
    !defined(ZP_PLATFORM_SOCKET_ZEPHYR) && !defined(ZP_PLATFORM_SOCKET_FREERTOS_PLUS_TCP) && \
    !defined(ZP_PLATFORM_SOCKET_ESP32) && !defined(ZP_PLATFORM_SOCKET_LWIP)
    _ZP_UNUSED(link);
    return false;
#else
    bool supported = false;
#if Z_FEATURE_LINK_TCP == 1
    supported = _z_unicast_link_inner_is_tcp(&link->_inner);
#endif
#if Z_FEATURE_LINK_TLS == 1
    supported = supported || _z_unicast_link_inner_is_tls(&link->_inner);
#endif
    if (!supported) {
        return false;
    }
    _z_sys_net_socket_t *socket = _z_unicast_link_get_socket(link);
    return _z_socket_set_blocking(socket, false) == _Z_RES_OK && _z_socket_set_blocking(socket, true) == _Z_RES_OK;
#endif
}

z_result_t _z_unicast_transport_manager_accept_peer(_z_unicast_transport_manager_t *manager, _z_unicast_link_t *link) {
    if (!_z_unicast_link_prepare_async_handshake(link)) {
        return _z_unicast_transport_manager_accept_peer_sync(manager, link);
    }
    return _z_unicast_transport_manager_start_pending_accept(manager, link);
}

z_result_t _z_unicast_transport_manager_connect_peer(_z_unicast_transport_manager_t *manager, _z_unicast_link_t *link,
                                                     _z_connect_peer_id_t locator_id) {
    _z_unicast_transport_peer_t peer = {0};
    _z_zbuf_t rx_leftover = _z_zbuf_null();
    _Z_RETURN_IF_ERR(_z_unicast_handshake_open(&peer, link, &manager->_parent->_session->_local_zid,
                                               manager->_parent->_session->_mode, &rx_leftover));

    z_result_t ret = _z_unicast_transport_manager_add_peer(manager, &peer, link, locator_id, &rx_leftover);
    // add_peer copies any leftover bytes into the peer rx buffer, so the handshake buffer is freed here.
    _z_zbuf_clear(&rx_leftover);
    return ret;
}

z_result_t _z_unicast_transport_manager_connect_peer_async(_z_unicast_transport_manager_t *manager,
                                                           _z_unicast_link_t *link, _z_connect_peer_id_t locator_id) {
    if (!_z_unicast_link_prepare_async_handshake(link)) {
        return _z_unicast_transport_manager_connect_peer(manager, link, locator_id);
    }
    return _z_unicast_transport_manager_start_pending_open(manager, link, locator_id);
}

z_result_t _z_unicast_transport_manager_close_peer(_z_unicast_transport_manager_t *manager,
                                                   _z_unicast_peer_slot_id_t peer_id,
                                                   const _z_close_reason_t *opt_reason,
                                                   _z_unicast_peer_slot_id_t *opt_next_peer_id) {
    if ((size_t)peer_id >= Z_MAX_NUM_UNICAST_PEERS) {
        return _Z_ERR_INVALID;
    }
    if (manager->_peer_states[peer_id] != _Z_UNICAST_SLOT_ESTABLISHED) {
        return _Z_ERR_INVALID;
    }
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_at(&manager->_peers, peer_id);
    _Z_INFO("Closing unicast peer " _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&peer->_remote_zid));
    _z_unicast_transport_manager_report_disconnected_event(manager, peer_id);
    _ZP_REMOVE_ONE(_z_unicast_lease_pqueue, &manager->_lease_pqueue, *_ == peer_id);
    _Z_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent));
    z_result_t ret = _Z_RES_OK;
    if (opt_reason != NULL) {
        _z_transport_message_t msg = _z_t_msg_make_close(*opt_reason, true);
        ret = _z_unicast_transport_manager_send_t_msg_to_peer(manager, &msg, peer_id);
    }
    _z_connect_peer_id_t locator_id = peer->_locator_id;
    if (opt_next_peer_id != NULL) {
        *opt_next_peer_id = _z_unicast_transport_peer_established_iter_next(manager, peer_id);
    }
    manager->_peer_states[peer_id] = _Z_UNICAST_SLOT_FREE;
    _z_unicast_transport_peer_hmap_remove_at(&manager->_peers, peer_id, NULL, NULL);
    _z_transport_manager_unlock(manager->_parent);
    _z_transport_manager_signal_closed_peer(manager->_parent, locator_id);
    return ret;
}

const z_clock_t *_z_unicast_transport_manager_check_lease(_z_unicast_transport_manager_t *manager) {
    z_clock_t now = z_clock_now();

    _z_close_reason_t reason = _Z_CLOSE_REASON_EXPIRED;
    while (!_z_unicast_lease_pqueue_is_empty(&manager->_lease_pqueue)) {
        _z_unicast_peer_slot_id_t peer_id = *_z_unicast_lease_pqueue_peek(&manager->_lease_pqueue);
        _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_at(&manager->_peers, peer_id);
        if (zp_clock_compare(&now, &peer->_lease_deadline) < 0) {
            break;
        }
        if (_z_unicast_peer_slot_state_is_pending(manager->_peer_states[peer_id])) {
            _z_unicast_transport_manager_abort_pending(manager, peer_id, _Z_ERR_TRANSPORT_RX_DURATION_EXPIRED);
            continue;
        }
        if (!peer->_received) {
            _Z_INFO("Lease expired for unicast peer %zu", (size_t)peer_id);
            z_result_t ret = _z_unicast_transport_manager_close_peer(manager, peer_id, &reason, NULL);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to remove expired unicast peer %zu", (size_t)peer_id);
                return NULL;
            }
        } else {
            _z_unicast_lease_pqueue_pop(&manager->_lease_pqueue, NULL);
            peer->_received = false;
            peer->_lease_deadline = now;
            z_clock_advance_ms(&peer->_lease_deadline, (unsigned long)peer->_lease_duration_ms);
            _z_unicast_lease_pqueue_push(&manager->_lease_pqueue, &peer_id);
        }
    }

    if (_z_unicast_lease_pqueue_is_empty(&manager->_lease_pqueue)) {
        return NULL;
    }
    _z_unicast_peer_slot_id_t next = *_z_unicast_lease_pqueue_peek(&manager->_lease_pqueue);
    return &_z_unicast_transport_peer_at(&manager->_peers, next)->_lease_deadline;
}

#if Z_FEATURE_UNICAST_PEER == 1
_z_fut_fn_result_t _zp_unicast_transport_listen_task_fn(void *unicast_transport_manager, _z_executor_t *executor) {
    _ZP_UNUSED(executor);
    _z_unicast_transport_manager_t *manager = (_z_unicast_transport_manager_t *)unicast_transport_manager;
    if (_z_unicast_transport_listener_vec_is_empty(&manager->_listeners)) {
        return _z_fut_fn_result_ready();
    }
    size_t i = manager->_listen_data.current_peer;
    if (manager->_listen_data.current_peer >= _z_unicast_transport_listener_vec_size(&manager->_listeners)) {
        manager->_listen_data.current_peer = 0;
    }
    _z_unicast_transport_listener_t *listener =
        _z_unicast_transport_listener_vec_at(&manager->_listeners, manager->_listen_data.current_peer);
    z_result_t ret = _z_unicast_transport_listener_listen(listener);
    if (ret != Z_RETRY_LATER && ret != _Z_RES_OK) {
        _Z_ERROR("Failed to listen on unicast listener %zu with error %d", i, ret);
    }
    manager->_listen_data.current_peer++;
    return ret == _Z_RES_OK ? _z_fut_fn_result_continue() : _z_fut_fn_result_wake_up_after(Z_ACCEPT_INTERVAL);
}
#endif  // Z_FEATURE_UNICAST_PEER == 1

#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
