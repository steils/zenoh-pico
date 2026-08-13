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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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

z_result_t _z_unicast_transport_manager_create(_z_unicast_transport_manager_t *manager,
                                               _z_transport_manager_t *parent) {
    memset(manager, 0, sizeof(_z_unicast_transport_manager_t));
#if Z_FEATURE_UNICAST_PEER == 1
    _z_listen_data_init(&manager->_listen_data);
    manager->_listen_task = _z_fut_handle_null();
#endif
    manager->_parent = parent;
    _z_unicast_transport_peer_hset_init(&manager->_peers);
    _z_unicast_lease_pqueue_init_with_ctx(&manager->_lease_pqueue, &manager->_peers);
    manager->_rx_buffer = _z_zbuf_null();
    return _Z_RES_OK;
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
    _ZP_IT_FOREACH (_z_unicast_transport_peer_hset, &manager->_peers, iter) {
        _z_unicast_transport_manager_send_t_msg_to_peer(manager, &msg, iter);
    }
}

void _z_unicast_transport_manager_clear(_z_unicast_transport_manager_t *manager) {
#if Z_FEATURE_UNICAST_PEER == 1
    if (!_z_fut_handle_is_null(manager->_listen_task)) {
        _z_runtime_cancel_fut(&manager->_parent->_session->_runtime, &manager->_listen_task);
        manager->_listen_task = _z_fut_handle_null();
    }
#endif
    // Peers must be destroyed before listeners: an accepted TLS peer's mbedtls SSL context shares the
    // listener's mbedtls_ssl_config (and its certificates/keys/RNG), so tearing down the listener first
    // would leave the peer contexts referencing freed memory during their own cleanup.
    _z_unicast_transport_peer_hset_destroy(&manager->_peers);
#if Z_FEATURE_UNICAST_PEER == 1
    _z_unicast_transport_listener_vec_destroy(&manager->_listeners);
#endif
    _z_unicast_lease_pqueue_destroy(&manager->_lease_pqueue);
    _z_zbuf_clear(&manager->_rx_buffer);
}

z_result_t _z_unicast_transport_manager_add_peer_inner(_z_unicast_transport_manager_t *manager,
                                                       _z_transport_unicast_establish_param_t *param,
                                                       _z_unicast_link_t *link, _z_connect_peer_id_t locator_id,
                                                       _z_zbuf_t *opt_rx_leftover) {
    if (_z_id_eq(&manager->_parent->_session->_local_zid, &param->_remote_zid)) {
        return _Z_ATTEMPT_SELF_CONNECT;
    } else if (_z_transport_manager_has_peer_with_zid(manager->_parent, &param->_remote_zid)) {
        return _Z_ALREADY_CONNECTED;
    }
    _z_unicast_transport_peer_t peer;
    memset(&peer, 0, sizeof(_z_unicast_transport_peer_t));
    peer._link = *link;
    peer._remote_zid = param->_remote_zid;
    peer._remote_whatami = (uint8_t)param->_remote_whatami;
    peer._lease_duration_ms = (uint32_t)param->_lease;
    peer._lease_deadline = z_clock_now();
    z_clock_advance_ms(&peer._lease_deadline, (unsigned long)peer._lease_duration_ms);
    peer._received = true;
    peer._transmitted = false;
    peer._sn_res = _z_sn_max(param->_seq_num_res);
#if Z_FEATURE_FRAGMENTATION == 1
    peer._sn_rx._reliable = param->_initial_sn_rx;
    peer._sn_rx._best_effort = param->_initial_sn_rx;
#endif
    peer._sn_tx._reliable = param->_initial_sn_tx;
    peer._sn_tx._best_effort = param->_initial_sn_tx;
    peer._locator_id = locator_id;
    peer._rx_msg_len = 0;
    uint16_t mtu = _z_unicast_link_get_mtu(link);
    mtu = mtu < param->_batch_size ? mtu : param->_batch_size;
    if (_z_unicast_link_is_streamed(&peer._link)) {
        // streamed links require a dedicated rx buffer per peer for managing leftover bytes
        _Z_CLEAN_RETURN_IF_ERR(_z_zbuf_init(&peer._rx_buffer, mtu),
                               _z_unicast_transport_peer_clear_without_link(&peer));
        // The handshake buffer may hold bytes the remote peer pipelined right after the handshake (e.g. its initial
        // declarations pushed on connect); copy them over so they are processed instead of dropped.
        // The handshake read was capped to the negotiated batch size, so the leftover always fits into this buffer.
        if (opt_rx_leftover != NULL && _z_zbuf_readable_len(opt_rx_leftover) > 0) {
            _z_zbuf_copy_bytes(&peer._rx_buffer, opt_rx_leftover);
        }
    } else {
        // datagram links can share a common rx buffer among all peers since they don't have leftover bytes to manage
        if (_z_zbuf_capacity(&manager->_rx_buffer) == 0) {
            _Z_CLEAN_RETURN_IF_ERR(_z_zbuf_init(&manager->_rx_buffer, Z_BATCH_UNICAST_SIZE),
                                   _z_unicast_transport_peer_clear_without_link(&peer));
        }
        peer._rx_buffer = _z_zbuf_null();
    }
#if Z_FEATURE_FRAGMENTATION == 1
    peer._patch = param->_patch < _Z_CURRENT_PATCH ? param->_patch : _Z_CURRENT_PATCH;
    _Z_CLEAN_RETURN_IF_ERR(_z_dbuf_init(&peer._dbuf), _z_unicast_transport_peer_clear_without_link(&peer));
#endif
#if Z_FEATURE_BATCHING == 1
    _Z_CLEAN_RETURN_IF_ERR(_z_wbuf_init(&peer._tx_buffer, mtu, false),
                           _z_unicast_transport_peer_clear_without_link(&peer));
#endif
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent),
                           _z_unicast_transport_peer_clear_without_link(&peer));
    _z_unicast_transport_peer_hset_iter_t iter = _z_unicast_transport_peer_hset_insert(&manager->_peers, &peer);
    _z_transport_manager_unlock(manager->_parent);
    if (iter == _z_unicast_transport_peer_hset_end(&manager->_peers)) {
        _z_unicast_transport_peer_clear_without_link(&peer);
        return _Z_CAPACITY_LIMIT_REACHED;
    }
    _Z_INFO("Added new unicast peer " _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&peer._remote_zid));
    _z_unicast_lease_pqueue_push(&manager->_lease_pqueue, &iter);
    _z_transport_manager_signal_opened_peer(manager->_parent, locator_id);
    _z_unicast_transport_manager_report_connected_event(manager, iter);
    return _Z_RES_OK;
}

z_result_t _z_unicast_transport_manager_add_peer(_z_unicast_transport_manager_t *manager,
                                                 _z_transport_unicast_establish_param_t *param, _z_unicast_link_t *link,
                                                 _z_connect_peer_id_t locator_id, _z_zbuf_t *opt_rx_leftover) {
    z_result_t ret = _z_unicast_transport_manager_add_peer_inner(manager, param, link, locator_id, opt_rx_leftover);
    if (ret == _Z_RES_OK) {
        _Z_INFO("Established connection with new unicast peer " _Z_ID_PRINT_FORMAT,
                _Z_ID_PRINT_ARGS(&param->_remote_zid));
    } else {
        _z_close_reason_t reason = _Z_CLOSE_REASON_GENERIC;
        if (ret == _Z_ATTEMPT_SELF_CONNECT) {
            reason = _Z_CLOSE_REASON_CONNECTION_TO_SELF;
        } else if (ret == _Z_ALREADY_CONNECTED) {
            reason = _Z_CLOSE_REASON_MAX_LINKS;
        } else if (ret == _Z_CAPACITY_LIMIT_REACHED) {
            reason = _Z_CLOSE_REASON_MAX_TRANSPORTS;
        }
        if (ret > _Z_RES_OK) {
            _Z_WARN("Rejecting peer " _Z_ID_PRINT_FORMAT " connection (%d)", _Z_ID_PRINT_ARGS(&param->_remote_zid),
                    ret);
        } else {
            _Z_ERROR("Failed to add peer " _Z_ID_PRINT_FORMAT " to transport manager: %d",
                     _Z_ID_PRINT_ARGS(&param->_remote_zid), ret);
        }
        _z_transport_message_t msg = _z_t_msg_make_close(reason, true);
        _z_unicast_link_send_t_msg(link, &msg);
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

static z_result_t _z_unicast_handshake_open(_z_transport_unicast_establish_param_t *param, _z_unicast_link_t *link,
                                            const _z_id_t *local_zid, z_whatami_t mode, _z_zbuf_t *rx_leftover) {
    *rx_leftover = _z_zbuf_null();
    z_clock_t recv_deadline = z_clock_now();
    z_clock_advance_ms(&recv_deadline, Z_TRANSPORT_CONNECT_TIMEOUT);

    uint16_t batch_size = _z_unicast_link_get_mtu(link);
    batch_size = batch_size < Z_BATCH_UNICAST_SIZE ? batch_size : Z_BATCH_UNICAST_SIZE;
    _z_transport_message_t ism = _z_t_msg_make_init_syn(mode, *local_zid, batch_size);
    param->_seq_num_res = ism._body._init._seq_num_res;  // The announced sn resolution
    param->_req_id_res = ism._body._init._req_id_res;    // The announced req id resolution
    param->_batch_size = ism._body._init._batch_size;    // The announced batch size

    // Encode and send the message
    _Z_DEBUG("Sending Z_INIT(Syn)");
    _Z_RETURN_IF_ERR(_z_unicast_link_send_t_msg(link, &ism));
    // Try to receive response
    // Create and prepare the buffer
    _z_zbuf_t zbf;
    _Z_RETURN_IF_ERR(_z_zbuf_init(&zbf, Z_BATCH_UNICAST_SIZE));

    _z_transport_message_t iam = {0};
    _Z_CLEAN_RETURN_IF_ERR(_z_unicast_link_recv_t_msg(link, &iam, &zbf, recv_deadline), _z_zbuf_clear(&zbf));
    if ((_Z_MID(iam._header) != _Z_MID_T_INIT) || !_Z_HAS_FLAG(iam._header, _Z_FLAG_T_INIT_A)) {
        _z_zbuf_clear(&zbf);
        _Z_ERROR_RETURN(_Z_ERR_MESSAGE_UNEXPECTED);
    }
    param->_remote_whatami = iam._body._init._whatami;
    _Z_DEBUG("Received Z_INIT(Ack)");
    // Any of the size parameters in the InitAck must be less or equal than the one in the InitSyn,
    // otherwise the InitAck message is considered invalid and it should be treated as a
    // CLOSE message with L==0 by the Initiating Peer -- the recipient of the InitAck message.
    z_result_t ret = _Z_RES_OK;
    if (iam._body._init._seq_num_res <= param->_seq_num_res) {
        param->_seq_num_res = iam._body._init._seq_num_res;
    } else {
        _Z_ERROR_LOG(_Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION);
        ret = _Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION;
    }
    if (iam._body._init._req_id_res <= param->_req_id_res) {
        param->_req_id_res = iam._body._init._req_id_res;
    } else {
        _Z_ERROR_LOG(_Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION);
        ret = _Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION;
    }
    if (iam._body._init._batch_size <= param->_batch_size) {
        param->_batch_size = iam._body._init._batch_size;
    } else {
        _Z_ERROR_LOG(_Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION);
        ret = _Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION;
    }
#if Z_FEATURE_FRAGMENTATION == 1
    if (iam._body._init._patch <= ism._body._init._patch) {
        param->_patch = iam._body._init._patch;
    } else {
        // TODO: Use a better error code?
        _Z_ERROR_LOG(_Z_ERR_GENERIC);
        ret = _Z_ERR_GENERIC;
    }
#endif
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&zbf);
        return ret;
    }
    param->_key_id_res = 0x08 << param->_key_id_res;
    param->_req_id_res = 0x08 << param->_req_id_res;

    // The initial SN at TX side
    z_random_fill(&param->_initial_sn_tx, sizeof(param->_initial_sn_tx));
    param->_initial_sn_tx &= _z_sn_modulo_mask(param->_seq_num_res);

    // Initialize the Local and Remote Peer IDs
    param->_remote_zid = iam._body._init._zid;

    // Create the OpenSyn message
    _z_zint_t lease = Z_TRANSPORT_LEASE;
    _z_zint_t initial_sn = param->_initial_sn_tx;
    _z_transport_message_t osm =
        _z_t_msg_make_open_syn(lease, initial_sn, _z_slice_view_deref(&iam._body._init._cookie));
    // Encode and send the message
    _Z_DEBUG("Sending Z_OPEN(Syn)");
    _Z_CLEAN_RETURN_IF_ERR(_z_unicast_link_send_t_msg(link, &osm), _z_zbuf_clear(&zbf));

    // Try to receive response.
    // Shrink the buffer to the negotiated batch size so that any bytes the remote peer pipelines together with the
    // OpenAck (e.g. its initial declarations pushed on connect) cannot exceed the peer rx buffer capacity
    // (param->_batch_size). This guarantees the leftover always fits into the peer rx buffer.
    _z_zbuf_clear(&zbf);
    _Z_RETURN_IF_ERR(_z_zbuf_init(&zbf, param->_batch_size));
    _z_transport_message_t oam = {0};
    _Z_CLEAN_RETURN_IF_ERR(_z_unicast_link_recv_t_msg(link, &oam, &zbf, recv_deadline), _z_zbuf_clear(&zbf));
    if ((_Z_MID(oam._header) != _Z_MID_T_OPEN) || !_Z_HAS_FLAG(oam._header, _Z_FLAG_T_OPEN_A)) {
        _z_zbuf_clear(&zbf);
        _Z_ERROR_LOG(_Z_ERR_MESSAGE_UNEXPECTED);
        return _Z_ERR_MESSAGE_UNEXPECTED;
    }
    // THIS LOG STRING USED IN TEST, change with caution
    _Z_DEBUG("Received Z_OPEN(Ack)");
    param->_lease = (oam._body._open._lease < Z_TRANSPORT_LEASE) ? oam._body._open._lease : Z_TRANSPORT_LEASE;
    // The initial SN at RX side. Initialize the session as we had already received
    // a message with a SN equal to initial_sn - 1.
    param->_initial_sn_rx = oam._body._open._initial_sn;
    if (_z_zbuf_readable_len(&zbf) > 0) {
        *rx_leftover = zbf;  // transfer ownership to the caller
    } else {
        _z_zbuf_clear(&zbf);
    }
    return _Z_RES_OK;
}

z_result_t _z_unicast_handshake_listen(_z_transport_unicast_establish_param_t *param, _z_unicast_link_t *link,
                                       const _z_id_t *local_zid, z_whatami_t mode) {
    z_clock_t recv_deadline = z_clock_now();
    z_clock_advance_ms(&recv_deadline, Z_TRANSPORT_ACCEPT_TIMEOUT);
    assert(mode == Z_WHATAMI_PEER);
    // Create and prepare the buffer
    _z_zbuf_t zbf;
    _Z_RETURN_IF_ERR(_z_zbuf_init(&zbf, Z_BATCH_UNICAST_SIZE));
    // Read t message from link
    _z_transport_message_t tmsg = {0};
    z_result_t ret = _z_unicast_link_recv_t_msg(link, &tmsg, &zbf, recv_deadline);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&zbf);
        return ret;
    }
    // Receive InitSyn
    if (_Z_MID(tmsg._header) != _Z_MID_T_INIT || _Z_HAS_FLAG(tmsg._header, _Z_FLAG_T_INIT_A)) {
        _z_zbuf_clear(&zbf);
        _Z_ERROR_RETURN(_Z_ERR_MESSAGE_UNEXPECTED);
    }
    _Z_DEBUG("Received Z_INIT(Syn)");
    // Encode InitAck
    _z_slice_t cookie = _z_slice_null();
    _z_transport_message_t iam = _z_t_msg_make_init_ack(mode, *local_zid, &cookie);

    // If the new node has less representing capabilities adjust settings
    if (tmsg._body._init._seq_num_res < iam._body._init._seq_num_res) {
        _Z_DEBUG("Adjusting SN resolution from %u to %u", iam._body._init._seq_num_res, tmsg._body._init._seq_num_res);
        iam._body._init._seq_num_res = tmsg._body._init._seq_num_res;
    }
    if (tmsg._body._init._req_id_res < iam._body._init._req_id_res) {
        _Z_DEBUG("Adjusting Req ID resolution from %u to %u", iam._body._init._req_id_res,
                 tmsg._body._init._req_id_res);
        iam._body._init._req_id_res = tmsg._body._init._req_id_res;
    }
    if (tmsg._body._init._batch_size < iam._body._init._batch_size) {
        _Z_DEBUG("Adjusting Batch Size from %u to %u", iam._body._init._batch_size, tmsg._body._init._batch_size);
        iam._body._init._batch_size = tmsg._body._init._batch_size;
    }

#if Z_FEATURE_FRAGMENTATION == 1
    if (iam._body._init._patch > tmsg._body._init._patch) {
        iam._body._init._patch = tmsg._body._init._patch;
    }
#endif
    param->_seq_num_res = iam._body._init._seq_num_res;
    param->_req_id_res = iam._body._init._req_id_res;
    param->_batch_size = iam._body._init._batch_size;
    param->_remote_zid = tmsg._body._init._zid;
    param->_remote_whatami = tmsg._body._init._whatami;
    param->_key_id_res = 0x08 << param->_key_id_res;
    param->_req_id_res = 0x08 << param->_req_id_res;
    // Send InitAck
    _Z_DEBUG("Sending Z_INIT(Ack)");
    ret = _z_unicast_link_send_t_msg(link, &iam);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&zbf);
        return ret;
    }
    // Read t message from link
    _z_zbuf_reset(&zbf);
    ret = _z_unicast_link_recv_t_msg(link, &tmsg, &zbf, recv_deadline);
    if (ret != _Z_RES_OK) {
        _z_zbuf_clear(&zbf);
        return ret;
    }
    // Receive OpenSyn
    if (_Z_MID(tmsg._header) != _Z_MID_T_OPEN || _Z_HAS_FLAG(tmsg._header, _Z_FLAG_T_INIT_A)) {
        _z_zbuf_clear(&zbf);
        _Z_ERROR_RETURN(_Z_ERR_MESSAGE_UNEXPECTED);
    }
    _Z_DEBUG("Received Z_OPEN(Syn)");
    // Process message
    param->_lease = (tmsg._body._open._lease < Z_TRANSPORT_LEASE) ? tmsg._body._open._lease : Z_TRANSPORT_LEASE;
    param->_initial_sn_rx = tmsg._body._open._initial_sn;
    _z_zbuf_clear(&zbf);

    // Encode OpenAck
    _z_zint_t lease = Z_TRANSPORT_LEASE;
    _z_zint_t initial_sn = param->_initial_sn_tx;
    _z_transport_message_t oam = _z_t_msg_make_open_ack(lease, initial_sn);

    // Encode and send the message
    _Z_DEBUG("Sending Z_OPEN(Ack)");
    ret = _z_unicast_link_send_t_msg(link, &oam);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    // Handshake finished
    return _Z_RES_OK;
}

z_result_t _z_unicast_transport_manager_connect_peer(_z_unicast_transport_manager_t *manager, _z_unicast_link_t *link,
                                                     _z_connect_peer_id_t locator_id) {
    _z_transport_unicast_establish_param_t param = {0};
    _z_zbuf_t rx_leftover = _z_zbuf_null();
    _Z_RETURN_IF_ERR(_z_unicast_handshake_open(&param, link, &manager->_parent->_session->_local_zid,
                                               manager->_parent->_session->_mode, &rx_leftover));

    z_result_t ret = _z_unicast_transport_manager_add_peer(manager, &param, link, locator_id, &rx_leftover);
    // add_peer copies any leftover bytes into the peer rx buffer, so the handshake buffer is freed here.
    _z_zbuf_clear(&rx_leftover);
    return ret;
}

z_result_t _z_unicast_transport_manager_close_peer(_z_unicast_transport_manager_t *manager,
                                                   _z_unicast_transport_peer_hset_iter_t peer_id,
                                                   const _z_close_reason_t *opt_reason,
                                                   _z_unicast_transport_peer_hset_iter_t *opt_next_peer_id) {
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_hset_at(&manager->_peers, peer_id);
    _Z_INFO("Closing unicast peer " _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&peer->_remote_zid));
    _z_unicast_transport_manager_report_disconnected_event(manager, peer_id);
    _ZP_REMOVE_ONE(_z_unicast_lease_pqueue, &manager->_lease_pqueue, *_ == peer_id);
    _Z_RETURN_IF_ERR(_z_transport_manager_lock(manager->_parent));
    z_result_t ret = _Z_RES_OK;
    if (opt_reason != NULL) {
        _z_transport_message_t msg = _z_t_msg_make_close(*opt_reason, true);
        ret = _z_unicast_transport_manager_send_t_msg_to_peer(manager, &msg, peer_id);
    }
    _z_connect_peer_id_t locator_id = _z_unicast_transport_peer_hset_at(&manager->_peers, peer_id)->_locator_id;
    _z_unicast_transport_peer_hset_remove_at(&manager->_peers, peer_id, NULL, opt_next_peer_id);
    _z_transport_manager_unlock(manager->_parent);
    _z_transport_manager_signal_closed_peer(manager->_parent, locator_id);
    return ret;
}

const z_clock_t *_z_unicast_transport_manager_check_lease(_z_unicast_transport_manager_t *manager) {
    z_clock_t now = z_clock_now();

    _z_close_reason_t reason = _Z_CLOSE_REASON_EXPIRED;
    while (!_z_unicast_lease_pqueue_is_empty(&manager->_lease_pqueue)) {
        _z_unicast_transport_peer_hset_iter_t peer_id = *_z_unicast_lease_pqueue_peek(&manager->_lease_pqueue);
        _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_hset_at(&manager->_peers, peer_id);
        if (zp_clock_compare(&now, &peer->_lease_deadline) <= 0) {
            break;
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
    _z_unicast_transport_peer_hset_iter_t next = *_z_unicast_lease_pqueue_peek(&manager->_lease_pqueue);
    return &_z_unicast_transport_peer_hset_at(&manager->_peers, next)->_lease_deadline;
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
