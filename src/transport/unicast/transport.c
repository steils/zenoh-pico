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

typedef enum {
    _Z_UNICAST_HANDSHAKE_ROLE_OPEN,
    _Z_UNICAST_HANDSHAKE_ROLE_ACCEPT,
} _z_unicast_handshake_role_t;

static z_result_t _z_unicast_handshake_start_open(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                                  _z_unicast_link_t *link, const _z_id_t *local_zid, z_whatami_t mode);
static void _z_unicast_handshake_start_accept(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                              z_whatami_t mode);

z_result_t _z_unicast_transport_manager_create(_z_unicast_transport_manager_t *manager,
                                               _z_transport_manager_t *parent) {
    memset(manager, 0, sizeof(_z_unicast_transport_manager_t));
#if Z_FEATURE_UNICAST_PEER == 1
    _z_listen_data_init(&manager->_listen_data);
    manager->_listen_task = _z_fut_handle_null();
#endif
    manager->_parent = parent;
    _z_address_to_unicast_transport_peer_hmap_init(&manager->_peers);
    _z_unicast_lease_pqueue_init_with_ctx(&manager->_lease_pqueue, &manager->_peers);
    manager->_rx_buffer = _z_zbuf_null();
    return _Z_RES_OK;
}

size_t _z_unicast_transport_peer_established_count(const _z_unicast_transport_manager_t *manager) {
    size_t count = 0;
    const _z_unicast_transport_peer_t *peer;
    _ZP_CONST_FOREACH_VAL_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_peers, peer,
                                    peer->_state == _Z_UNICAST_PEER_ESTABLISHED) {
        count++;
    }
    return count;
}

size_t _z_unicast_transport_manager_get_pending_count(const _z_unicast_transport_manager_t *manager) {
    size_t count = 0;
    const _z_unicast_transport_peer_t *peer;
    _ZP_CONST_FOREACH_VAL_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_peers, peer,
                                    peer->_state != _Z_UNICAST_PEER_ESTABLISHED) {
        count++;
    }
    return count;
}

static z_result_t _z_unicast_transport_manager_start_pending(_z_unicast_transport_manager_t *manager,
                                                             _z_unicast_link_t *link, _z_connect_peer_id_t locator_id,
                                                             _z_unicast_handshake_role_t role) {
    _z_unicast_transport_peer_t peer = {
        ._link = _z_unicast_link_null(),
        ._rx_buffer = _z_zbuf_null(),
        ._locator_id = locator_id,
    };
    _Z_RETURN_IF_ERR(_z_zbuf_init(&peer._rx_buffer, Z_BATCH_UNICAST_SIZE));

    z_result_t ret;
    peer._lease_deadline = z_clock_now();
    z_clock_advance_ms(&peer._lease_deadline, role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN ? Z_TRANSPORT_CONNECT_TIMEOUT
                                                                                     : Z_TRANSPORT_ACCEPT_TIMEOUT);
    _z_session_t *session = manager->_parent->_session;
    _z_unicast_peer_state_t state;
    if (role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN) {
        ret = _z_unicast_handshake_start_open(&peer, &state, link, &session->_local_zid, session->_mode);
        if (ret != _Z_RES_OK) {
            _z_zbuf_clear(&peer._rx_buffer);
            return ret;
        }
    } else {
        _z_unicast_handshake_start_accept(&peer, &state, session->_mode);
    }
    ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&peer._rx_buffer);
        return ret;
    }

    peer._link = *link;
    peer._state = state;
    _z_link_address_t key = _z_unicast_peer_key_numeric(manager->_next_numeric_peer_id++);
    _z_address_to_unicast_transport_peer_hmap_iter_t peer_id =
        _z_address_to_unicast_transport_peer_hmap_insert(&manager->_peers, &key, &peer);
    _z_transport_manager_unlock(manager->_parent);
    if (peer_id == _z_address_to_unicast_transport_peer_hmap_end(&manager->_peers)) {
        _z_zbuf_clear(&peer._rx_buffer);
        return _Z_CAPACITY_LIMIT_REACHED;
    }
    *link = _z_unicast_link_null();

    (void)_z_unicast_lease_pqueue_push(&manager->_lease_pqueue, &peer_id);
    _z_transport_manager_signal_pending_peer(manager->_parent, locator_id);
    return _Z_RES_OK;
}

static z_result_t _z_unicast_transport_manager_start_pending_open(_z_unicast_transport_manager_t *manager,
                                                                  _z_unicast_link_t *link,
                                                                  _z_connect_peer_id_t locator_id) {
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
    _z_address_to_unicast_transport_peer_hmap_elem_t *entry;
    _ZP_FOREACH_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_peers, entry,
                          entry->val._state == _Z_UNICAST_PEER_ESTABLISHED) {
        _z_unicast_transport_manager_send_t_msg_to_peer(manager, &msg, entry_iter);
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
    _z_address_to_unicast_transport_peer_hmap_destroy(&manager->_peers);
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

static z_result_t _z_unicast_transport_peer_init_established(_z_unicast_transport_peer_t *peer, uint16_t capacity) {
    peer->_received = true;
    peer->_transmitted = false;
#if Z_FEATURE_FRAGMENTATION == 1
    _Z_RETURN_IF_ERR(_z_dbuf_init(&peer->_dbuf));
#endif
#if Z_FEATURE_BATCHING == 1
    z_result_t ret = _z_wbuf_init(&peer->_tx_buffer, capacity, false);
    if (ret != _Z_RES_OK) {
#if Z_FEATURE_FRAGMENTATION == 1
        _z_dbuf_clear(&peer->_dbuf);
#endif
        return ret;
    }
#else
    _ZP_UNUSED(capacity);
#endif
    return _Z_RES_OK;
}

static void _z_unicast_transport_peer_set_lease_deadline(_z_unicast_transport_peer_t *peer) {
    peer->_lease_deadline = z_clock_now();
    z_clock_advance_ms(&peer->_lease_deadline, (unsigned long)peer->_lease_duration_ms);
}

static void _z_unicast_transport_manager_report_added_peer(_z_unicast_transport_manager_t *manager,
                                                           _z_address_to_unicast_transport_peer_hmap_iter_t id) {
    const _z_unicast_transport_peer_t *peer =
        &_z_address_to_unicast_transport_peer_hmap_const_at(&manager->_peers, id)->val;
    _Z_INFO("Added new unicast peer " _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&peer->_remote_zid));
    _ZP_REMOVE_ONE(_z_unicast_lease_pqueue, &manager->_lease_pqueue, *_ == id);
    if (_z_unicast_lease_pqueue_size(&manager->_lease_pqueue) >= Z_MAX_NUM_UNICAST_PEERS) {
        _Z_ERROR("Failed to add unicast peer %zu to the lease queue: capacity limit reached", (size_t)id);
    } else {
        (void)_z_unicast_lease_pqueue_push(&manager->_lease_pqueue, &id);
    }
    _z_transport_manager_signal_opened_peer(manager->_parent, peer->_locator_id);
    _z_unicast_transport_manager_report_connected_event(manager, id);
}

z_result_t _z_unicast_transport_manager_establish_pending(_z_unicast_transport_manager_t *manager,
                                                          _z_address_to_unicast_transport_peer_hmap_iter_t id) {
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, id)->val;
    if (peer->_state == _Z_UNICAST_PEER_ESTABLISHED) {
        return _Z_ERR_INVALID;
    }
    uint16_t mtu = _z_unicast_link_get_mtu(&peer->_link);
    uint16_t capacity = mtu < peer->_batch_size ? mtu : peer->_batch_size;
    _Z_RETURN_IF_ERR(_z_unicast_transport_peer_init_established(peer, capacity));
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent),
                           _z_unicast_transport_peer_clear_established(peer));
    z_result_t ret = _z_unicast_transport_manager_validate_peer(manager, &peer->_remote_zid);
    if (ret != _Z_RES_OK) {
        _z_transport_manager_unlock(manager->_parent);
        _z_unicast_transport_reject_peer(&peer->_link, &peer->_remote_zid, ret);
        _z_unicast_transport_peer_clear_established(peer);
        return ret;
    }
    _z_unicast_transport_peer_set_lease_deadline(peer);
    peer->_state = _Z_UNICAST_PEER_ESTABLISHED;
    _z_transport_manager_unlock(manager->_parent);

    _z_unicast_transport_manager_report_added_peer(manager, id);
    return _Z_RES_OK;
}

static z_result_t _z_unicast_transport_manager_add_peer_inner(_z_unicast_transport_manager_t *manager,
                                                              _z_unicast_transport_peer_t *peer,
                                                              _z_unicast_link_t *link, _z_connect_peer_id_t locator_id,
                                                              _z_zbuf_t *opt_rx_leftover) {
    uint16_t mtu = _z_unicast_link_get_mtu(link);
    uint16_t capacity = mtu < peer->_batch_size ? mtu : peer->_batch_size;
    _Z_RETURN_IF_ERR(_z_unicast_transport_peer_init_established(peer, capacity));
    _z_zbuf_t rx_buffer = _z_zbuf_null();
    z_result_t ret = _Z_RES_OK;
    if (_z_unicast_link_is_streamed(link)) {
        ret = _z_zbuf_init(&rx_buffer, capacity);
        if (ret == _Z_RES_OK && opt_rx_leftover != NULL && _z_zbuf_readable_len(opt_rx_leftover) > 0) {
            _z_zbuf_copy_bytes(&rx_buffer, opt_rx_leftover);
        }
    } else if (_z_zbuf_capacity(&manager->_rx_buffer) == 0) {
        ret = _z_zbuf_init(&manager->_rx_buffer, Z_BATCH_UNICAST_SIZE);
    }
    if (ret != _Z_RES_OK) {
        _z_unicast_transport_peer_clear_established(peer);
        return ret;
    }
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent), _z_zbuf_clear(&rx_buffer);
                           _z_unicast_transport_peer_clear_established(peer));
    ret = _z_unicast_transport_manager_validate_peer(manager, &peer->_remote_zid);
    if (ret != _Z_RES_OK) {
        _z_transport_manager_unlock(manager->_parent);
        _z_zbuf_clear(&rx_buffer);
        _z_unicast_transport_peer_clear_established(peer);
        return ret;
    }

    _z_unicast_transport_peer_set_lease_deadline(peer);
    peer->_link = *link;
    peer->_rx_buffer = rx_buffer;
    peer->_locator_id = locator_id;
    peer->_state = _Z_UNICAST_PEER_ESTABLISHED;
    _z_link_address_t key = _z_unicast_peer_key_numeric(manager->_next_numeric_peer_id++);
    _z_address_to_unicast_transport_peer_hmap_iter_t id =
        _z_address_to_unicast_transport_peer_hmap_insert(&manager->_peers, &key, peer);
    if (id == _z_address_to_unicast_transport_peer_hmap_end(&manager->_peers)) {
        _z_transport_manager_unlock(manager->_parent);
        _z_zbuf_clear(&rx_buffer);
        _z_unicast_transport_peer_clear_established(peer);
        return _Z_CAPACITY_LIMIT_REACHED;
    }
    *link = _z_unicast_link_null();
    _z_transport_manager_unlock(manager->_parent);

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

static void _z_unicast_handshake_log_message(const char *action, const _z_transport_message_t *message) {
    if (_Z_MID(message->_header) == _Z_MID_T_INIT) {
        _Z_DEBUG("%s Z_INIT(%s)", action, _Z_HAS_FLAG(message->_header, _Z_FLAG_T_INIT_A) ? "Ack" : "Syn");
    } else if (_Z_MID(message->_header) == _Z_MID_T_OPEN) {
        _Z_DEBUG("%s Z_OPEN(%s)", action, _Z_HAS_FLAG(message->_header, _Z_FLAG_T_OPEN_A) ? "Ack" : "Syn");
    }
}

static z_result_t _z_unicast_handshake_send(_z_unicast_link_t *link, const _z_transport_message_t *message) {
    _z_unicast_handshake_log_message("Sending", message);
    return _z_unicast_link_send_t_msg(link, message);
}

static z_result_t _z_unicast_handshake_start_open(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                                  _z_unicast_link_t *link, const _z_id_t *local_zid, z_whatami_t mode) {
    *state = _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK;
    uint16_t batch_size = _z_unicast_link_get_mtu(link);
    batch_size = batch_size < Z_BATCH_UNICAST_SIZE ? batch_size : Z_BATCH_UNICAST_SIZE;
    _z_transport_message_t output = _z_t_msg_make_init_syn(mode, *local_zid, batch_size);
    peer->_sn_res = _z_sn_max(output._body._init._seq_num_res);
    peer->_batch_size = output._body._init._batch_size;
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_patch = output._body._init._patch;
#endif
    return _z_unicast_handshake_send(link, &output);
}

static void _z_unicast_handshake_start_accept(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                              z_whatami_t mode) {
    assert(mode == Z_WHATAMI_PEER);
    *state = _Z_UNICAST_HS_ACCEPT_WAIT_INIT;
    _ZP_UNUSED(peer);
}

static z_result_t _z_unicast_handshake_open_init_ack(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                                     _z_unicast_link_t *link, const _z_transport_message_t *input) {
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

    _z_transport_message_t output =
        _z_t_msg_make_open_syn(Z_TRANSPORT_LEASE, peer->_sn_tx._reliable, _z_slice_view_deref(&init_ack->_cookie));
    *state = _Z_UNICAST_HS_OPEN_WAIT_OPEN_ACK;
    return _z_unicast_handshake_send(link, &output);
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

static z_result_t _z_unicast_handshake_accept_init(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                                   _z_unicast_link_t *link, const _z_id_t *local_zid,
                                                   z_whatami_t local_whatami, const _z_transport_message_t *input) {
    if ((_Z_MID(input->_header) != _Z_MID_T_INIT) || _Z_HAS_FLAG(input->_header, _Z_FLAG_T_INIT_A)) {
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }

    _z_slice_t cookie = _z_slice_null();
    _z_transport_message_t output = _z_t_msg_make_init_ack(local_whatami, *local_zid, &cookie);
    if (input->_body._init._seq_num_res < output._body._init._seq_num_res) {
        output._body._init._seq_num_res = input->_body._init._seq_num_res;
    }
    if (input->_body._init._req_id_res < output._body._init._req_id_res) {
        output._body._init._req_id_res = input->_body._init._req_id_res;
    }
    if (input->_body._init._batch_size < output._body._init._batch_size) {
        output._body._init._batch_size = input->_body._init._batch_size;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    if (input->_body._init._patch < output._body._init._patch) {
        output._body._init._patch = input->_body._init._patch;
    }
#endif

    peer->_sn_res = _z_sn_max(output._body._init._seq_num_res);
    peer->_batch_size = output._body._init._batch_size;
    peer->_remote_zid = input->_body._init._zid;
    peer->_remote_whatami = input->_body._init._whatami;
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_patch = output._body._init._patch;
#endif
    *state = _Z_UNICAST_HS_ACCEPT_WAIT_OPEN;
    return _z_unicast_handshake_send(link, &output);
}

static z_result_t _z_unicast_handshake_accept_open(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                                   const _z_transport_message_t *input, bool *complete) {
    if ((_Z_MID(input->_header) != _Z_MID_T_OPEN) || _Z_HAS_FLAG(input->_header, _Z_FLAG_T_OPEN_A)) {
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_sn_rx._reliable = input->_body._open._initial_sn;
    peer->_sn_rx._best_effort = input->_body._open._initial_sn;
#endif
    peer->_lease_duration_ms =
        (uint32_t)(input->_body._open._lease < Z_TRANSPORT_LEASE ? input->_body._open._lease : Z_TRANSPORT_LEASE);
    _z_transport_message_t output = _z_t_msg_make_open_ack(Z_TRANSPORT_LEASE, peer->_sn_tx._reliable);
    _Z_RETURN_IF_ERR(_z_unicast_handshake_send(link, &output));
    *complete = true;
    return _Z_RES_OK;
}

z_result_t _z_unicast_handshake_handle_input(_z_unicast_transport_peer_t *peer, _z_unicast_peer_state_t *state,
                                             _z_unicast_link_t *link, const _z_id_t *local_zid,
                                             z_whatami_t local_whatami, const _z_transport_message_t *input,
                                             bool *complete) {
    *complete = false;
    switch (*state) {
        case _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK:
            return _z_unicast_handshake_open_init_ack(peer, state, link, input);
        case _Z_UNICAST_HS_OPEN_WAIT_OPEN_ACK:
            return _z_unicast_handshake_open_open_ack(peer, input, complete);
        case _Z_UNICAST_HS_ACCEPT_WAIT_INIT:
            return _z_unicast_handshake_accept_init(peer, state, link, local_zid, local_whatami, input);
        case _Z_UNICAST_HS_ACCEPT_WAIT_OPEN:
            return _z_unicast_handshake_accept_open(peer, link, input, complete);
        default:
            return _Z_ERR_INVALID;
    }
}

static z_result_t _z_unicast_handshake_drive_sync(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                                  const _z_id_t *local_zid, z_whatami_t mode,
                                                  _z_unicast_handshake_role_t role, _z_zbuf_t *opt_rx_leftover) {
    if (opt_rx_leftover != NULL) {
        *opt_rx_leftover = _z_zbuf_null();
    }
    _z_unicast_peer_state_t state;
    z_clock_t deadline = z_clock_now();
    z_clock_advance_ms(
        &deadline, role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN ? Z_TRANSPORT_CONNECT_TIMEOUT : Z_TRANSPORT_ACCEPT_TIMEOUT);

    bool complete = false;

    _z_zbuf_t zbuf;
    z_result_t ret = _z_zbuf_init(&zbuf, Z_BATCH_UNICAST_SIZE);
    if (ret != _Z_RES_OK) {
        return ret;
    }

    if (role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN) {
        ret = _z_unicast_handshake_start_open(peer, &state, link, local_zid, mode);
    } else {
        _z_unicast_handshake_start_accept(peer, &state, mode);
    }

    while (ret == _Z_RES_OK && !complete) {
        _z_unicast_peer_state_t previous_state = state;
        _z_transport_message_t input = {0};
        ret = _z_unicast_link_recv_t_msg(link, &input, &zbuf, deadline);
        if (ret != _Z_RES_OK) {
            break;
        }
        _z_unicast_handshake_log_message("Received", &input);
        ret = _z_unicast_handshake_handle_input(peer, &state, link, local_zid, mode, &input, &complete);
        if (ret != _Z_RES_OK) {
            break;
        }

        if (previous_state == _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK) {
            _z_zbuf_clear(&zbuf);
            ret = _z_zbuf_init(&zbuf, peer->_batch_size);
            if (ret != _Z_RES_OK) {
                break;
            }
        } else if (previous_state == _Z_UNICAST_HS_ACCEPT_WAIT_INIT) {
            _z_zbuf_reset(&zbuf);
        }
    }

    if (ret == _Z_RES_OK && role == _Z_UNICAST_HANDSHAKE_ROLE_OPEN && opt_rx_leftover != NULL &&
        _z_zbuf_readable_len(&zbuf) > 0) {
        *opt_rx_leftover = zbuf;
    } else {
        _z_zbuf_clear(&zbuf);
    }
    return ret;
}

static z_result_t _z_unicast_handshake_open(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                            const _z_id_t *local_zid, z_whatami_t mode, _z_zbuf_t *rx_leftover) {
    *rx_leftover = _z_zbuf_null();
    return _z_unicast_handshake_drive_sync(peer, link, local_zid, mode, _Z_UNICAST_HANDSHAKE_ROLE_OPEN, rx_leftover);
}

z_result_t _z_unicast_handshake_listen(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                       const _z_id_t *local_zid, z_whatami_t mode) {
    return _z_unicast_handshake_drive_sync(peer, link, local_zid, mode, _Z_UNICAST_HANDSHAKE_ROLE_ACCEPT, NULL);
}

static z_result_t _z_unicast_transport_manager_accept_peer_sync(_z_unicast_transport_manager_t *manager,
                                                                _z_unicast_link_t *link) {
    _z_session_t *session = manager->_parent->_session;
    _z_unicast_transport_peer_t peer = {0};
    _Z_RETURN_IF_ERR(_z_unicast_handshake_listen(&peer, link, &session->_local_zid, session->_mode));

    return _z_unicast_transport_manager_add_peer(manager, &peer, link, _ZP_CONNECT_PEER_ID_FROM_LISTEN, NULL);
}

static bool _z_unicast_link_supports_async_handshake(const _z_unicast_link_t *link) {
    bool supported = false;
    _ZP_UNUSED(link);
#if Z_FEATURE_LINK_TCP == 1
    supported = _z_unicast_link_inner_is_tcp(&link->_inner);
#endif
#if Z_FEATURE_LINK_TLS == 1
    supported = supported || _z_unicast_link_inner_is_tls(&link->_inner);
#endif
    return supported;
}

z_result_t _z_unicast_transport_manager_accept_peer(_z_unicast_transport_manager_t *manager, _z_unicast_link_t *link) {
    if (!_z_unicast_link_supports_async_handshake(link)) {
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
    if (!_z_unicast_link_supports_async_handshake(link)) {
        return _z_unicast_transport_manager_connect_peer(manager, link, locator_id);
    }
    return _z_unicast_transport_manager_start_pending_open(manager, link, locator_id);
}

z_result_t _z_unicast_transport_manager_close_peer(_z_unicast_transport_manager_t *manager,
                                                   _z_address_to_unicast_transport_peer_hmap_iter_t peer_id,
                                                   const _z_close_reason_t *opt_reason,
                                                   _z_address_to_unicast_transport_peer_hmap_iter_t *opt_next_peer_id) {
    if (opt_next_peer_id != NULL) {
        *opt_next_peer_id = _z_address_to_unicast_transport_peer_hmap_end(&manager->_peers);
    }
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
    bool pending = peer->_state != _Z_UNICAST_PEER_ESTABLISHED;
    if (pending) {
        _Z_INFO("Closing pending unicast handshake %zu", (size_t)peer_id);
    } else {
        _Z_INFO("Closing unicast peer " _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&peer->_remote_zid));
        _z_unicast_transport_manager_report_disconnected_event(manager, peer_id);
    }

    _Z_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent));
    _ZP_REMOVE_ONE(_z_unicast_lease_pqueue, &manager->_lease_pqueue, *_ == peer_id);
    z_result_t ret = _Z_RES_OK;
    if (!pending && opt_reason != NULL) {
        _z_transport_message_t msg = _z_t_msg_make_close(*opt_reason, true);
        ret = _z_unicast_transport_manager_send_t_msg_to_peer(manager, &msg, peer_id);
    }
    _z_connect_peer_id_t locator_id = peer->_locator_id;
    _z_address_to_unicast_transport_peer_hmap_remove_at(&manager->_peers, peer_id, NULL, opt_next_peer_id);
    _z_transport_manager_unlock(manager->_parent);
    _z_transport_manager_signal_closed_peer(manager->_parent, locator_id);
    return ret;
}

const z_clock_t *_z_unicast_transport_manager_check_lease(_z_unicast_transport_manager_t *manager) {
    z_clock_t now = z_clock_now();

    _z_close_reason_t reason = _Z_CLOSE_REASON_EXPIRED;
    while (!_z_unicast_lease_pqueue_is_empty(&manager->_lease_pqueue)) {
        _z_address_to_unicast_transport_peer_hmap_iter_t peer_id =
            *_z_unicast_lease_pqueue_peek(&manager->_lease_pqueue);
        _z_unicast_transport_peer_t *peer =
            &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
        if (zp_clock_compare(&now, &peer->_lease_deadline) < 0) {
            break;
        }
        if (peer->_state != _Z_UNICAST_PEER_ESTABLISHED) {
            z_result_t ret = _z_unicast_transport_manager_close_peer(manager, peer_id, NULL, NULL);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to remove expired pending handshake %zu", (size_t)peer_id);
                return NULL;
            }
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
    _z_address_to_unicast_transport_peer_hmap_iter_t next = *_z_unicast_lease_pqueue_peek(&manager->_lease_pqueue);
    return &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, next)->val._lease_deadline;
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
