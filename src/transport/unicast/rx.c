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

#include "zenoh-pico/transport/rx.h"

#include <stddef.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/transport/unicast/rx.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/transport/unicast/tx.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1

static z_result_t _z_unicast_transport_manager_handle_handshake_message(
    _z_unicast_transport_manager_t *manager, const _z_transport_message_t *message,
    _z_address_to_unicast_transport_peer_hmap_iter_t peer_id);

static z_result_t _z_unicast_transport_manager_handle_transport_message(
    _z_unicast_transport_manager_t *manager, _z_transport_message_t *t_msg,
    _z_address_to_unicast_transport_peer_hmap_iter_t peer_id) {
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
    if (_z_unicast_peer_state_is_pending(peer->_state)) {
        return _z_unicast_transport_manager_handle_handshake_message(manager, t_msg, peer_id);
    }
    peer->_received = true;
    switch (_Z_MID(t_msg->_header)) {
        case _Z_MID_T_FRAME:
            _Z_DEBUG("Received Z_FRAME message");
            return _z_transport_handle_frame(manager->_parent, t_msg->_header, &t_msg->_body._frame,
#if Z_FEATURE_FRAGMENTATION == 1
                                             &peer->_dbuf, &peer->_sn_rx,
#endif
                                             peer_id);
        case _Z_MID_T_FRAGMENT:
            _Z_DEBUG("Received Z_FRAGMENT message");
#if Z_FEATURE_FRAGMENTATION == 1
            return _z_transport_handle_fragment(manager->_parent, t_msg->_header, &t_msg->_body._fragment, &peer->_dbuf,
                                                &peer->_sn_rx, peer->_sn_res, peer->_patch, peer_id);
#else
            _Z_INFO("Received Z_FRAGMENT message but fragmentation feature is not enabled, dropping the message");
            return _Z_RES_OK;
#endif
        case _Z_MID_T_KEEP_ALIVE:
            _Z_DEBUG("Received Z_KEEP_ALIVE message");
            return _Z_RES_OK;
        case _Z_MID_T_INIT:
            // Do nothing, zenoh clients are not expected to handle accept messages on established sessions
            return _Z_RES_OK;  // should we rather return an error here and close the transport ?
        case _Z_MID_T_OPEN:
            // Do nothing, zenoh clients are not expected to handle accept messages on established sessions
            return _Z_RES_OK;  // should we rather return an error here and close the transport ?
        case _Z_MID_T_CLOSE: {
            _Z_INFO("Closing session as requested by the remote peer due to reason code %d",
                    (_z_close_reason_t)t_msg->_body._close._reason);
            return Z_REMOTE_PEER_SENT_CLOSE;
        }
        default: {
            _Z_INFO("WARNING: Unknown transport message ID");
            return _Z_RES_OK;  // should we rather return an error here and close the transport ?
        }
    }
}

static z_result_t _z_unicast_link_recv_zbuf_stream(_z_unicast_link_t *link, _z_zbuf_t *zbf, uint16_t *msg_len) {
    size_t readable_len = _z_zbuf_readable_len(zbf);
    size_t part_len = *msg_len == 0 ? _Z_MSG_LEN_ENC_SIZE : *msg_len;
    size_t remaining_capacity = _z_zbuf_writable_space_left(zbf) + readable_len;
    if (remaining_capacity < part_len) {
        _z_zbuf_compact(zbf);
        remaining_capacity = _z_zbuf_writable_space_left(zbf) + _z_zbuf_readable_len(zbf);
        if (remaining_capacity < part_len) {
            _Z_ERROR_LOG(_Z_ERR_TRANSPORT_NO_SPACE);
            return _Z_ERR_TRANSPORT_NO_SPACE;
        }
    }
    if (_z_unicast_link_recv_zbuf(link, zbf)) {
        if (*msg_len == 0) {
            if (_z_zbuf_readable_len(zbf) < _Z_MSG_LEN_ENC_SIZE) {
                return Z_NO_DATA;  // Not enough data to read the message length
            } else {
                *msg_len =
                    _z_transport_read_stream_size(zbf);  // this moves the read pointer forward by _Z_MSG_LEN_ENC_SIZE
            }
        }
        return _z_zbuf_readable_len(zbf) >= *msg_len ? _Z_RES_OK : Z_NO_DATA;
    } else {
        return Z_NO_DATA;  // with current link implementation there is no way to distinguish between a read error and
                           // timeout
    }
}

static z_result_t _z_unicast_pending_handshake_resize_rx(_z_unicast_transport_peer_t *peer, size_t capacity) {
    if (_z_zbuf_capacity(&peer->_rx_buffer) == capacity) {
        return _Z_RES_OK;
    }
    if (_z_zbuf_readable_len(&peer->_rx_buffer) > capacity) {
        return _Z_ERR_TRANSPORT_NO_SPACE;
    }
    _z_zbuf_t resized = _z_zbuf_null();
    _Z_RETURN_IF_ERR(_z_zbuf_init(&resized, capacity));
    _z_zbuf_copy_bytes(&resized, &peer->_rx_buffer);
    _z_zbuf_clear(&peer->_rx_buffer);
    peer->_rx_buffer = resized;
    return _Z_RES_OK;
}

static z_result_t _z_unicast_transport_manager_commit_pending(
    _z_unicast_transport_manager_t *manager, _z_address_to_unicast_transport_peer_hmap_iter_t slot_id) {
    return _z_unicast_transport_manager_establish_pending(manager, slot_id);
}

static z_result_t _z_unicast_transport_manager_handle_handshake_message(
    _z_unicast_transport_manager_t *manager, const _z_transport_message_t *message,
    _z_address_to_unicast_transport_peer_hmap_iter_t slot_id) {
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, slot_id)->val;
    _z_transport_message_t output = {0};
    bool has_output = false;
    bool complete = false;
    _z_session_t *session = manager->_parent->_session;

    z_result_t ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    _z_unicast_peer_state_t previous_state = peer->_state;
    ret = _z_unicast_handshake_handle_input(peer, &peer->_state, &session->_local_zid, session->_mode, message, &output,
                                            &has_output, &complete);
    _z_transport_manager_unlock(manager->_parent);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    if (has_output) {
        ret = _z_unicast_link_send_t_msg(&peer->_link, &output);
        if (ret != _Z_RES_OK) {
            return ret;
        }
    }
    if (previous_state == _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK || previous_state == _Z_UNICAST_HS_ACCEPT_WAIT_INIT) {
        ret = _z_unicast_pending_handshake_resize_rx(peer, peer->_batch_size);
        if (ret != _Z_RES_OK) {
            return ret;
        }
    }
    return complete ? _z_unicast_transport_manager_commit_pending(manager, slot_id) : _Z_RES_OK;
}

static z_result_t _z_unicast_link_recv_zbuf_datagram(_z_unicast_link_t *link, _z_zbuf_t *zbf) {
    _z_zbuf_reset(zbf);
    if (_z_unicast_link_recv_zbuf(link, zbf)) {
        return _z_zbuf_readable_len(zbf) > 0 ? _Z_RES_OK : Z_NO_DATA;
    } else {
        return Z_NO_DATA;  // with current link implementation there is no way to distinguish between a read error and
                           // timeout
    }
}

z_result_t _z_unicast_link_recv_t_msg(_z_unicast_link_t *link, _z_transport_message_t *t_msg, _z_zbuf_t *zbf,
                                      z_clock_t recv_deadline) {
    z_result_t ret = Z_NO_DATA;
    uint16_t msg_len = 0;
    while (ret == Z_NO_DATA) {
        ret = _z_unicast_link_is_streamed(link) ? _z_unicast_link_recv_zbuf_stream(link, zbf, &msg_len)
                                                : _z_unicast_link_recv_zbuf_datagram(link, zbf);
        if (ret == Z_NO_DATA) {
            z_clock_t now = z_clock_now();
            if (zp_clock_compare(&now, &recv_deadline) >= 0) {
                break;
            }
        }
    }

    if (ret == _Z_RES_OK) {
        ret = _z_transport_message_decode(t_msg, zbf);
    } else {
        _Z_ERROR_LOG(ret);
    }

    return ret;
}

static z_result_t _z_unicast_transport_peer_recv(_z_unicast_transport_manager_t *manager,
                                                 _z_address_to_unicast_transport_peer_hmap_iter_t peer_id) {
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
    return _z_unicast_link_is_streamed(&peer->_link)
               ? _z_unicast_link_recv_zbuf_stream(&peer->_link, &peer->_rx_buffer, &peer->_rx_msg_len)
               : _z_unicast_link_recv_zbuf_datagram(&peer->_link, &manager->_rx_buffer);
}

static z_result_t _z_unicast_transport_peer_process_messages(_z_unicast_transport_manager_t *manager,
                                                             _z_address_to_unicast_transport_peer_hmap_iter_t peer_id) {
    _z_unicast_transport_peer_t *peer = &_z_address_to_unicast_transport_peer_hmap_at(&manager->_peers, peer_id)->val;
    bool has_more_messages = true;
    uint16_t *rx_msg_len_ptr = NULL;
    _z_zbuf_t *rx_buffer = &manager->_rx_buffer;
    if (_z_unicast_link_is_streamed(&peer->_link)) {
        rx_buffer = &peer->_rx_buffer;
        rx_msg_len_ptr = &peer->_rx_msg_len;
    }
    while (has_more_messages) {
        _z_transport_message_t t_msg = {0};
        _Z_RETURN_IF_ERR(_z_transport_read_message(rx_buffer, rx_msg_len_ptr, &t_msg, &has_more_messages));
        _Z_RETURN_IF_ERR(_z_unicast_transport_manager_handle_transport_message(manager, &t_msg, peer_id));
    }
    return _Z_RES_OK;
}

z_result_t _z_unicast_transport_manager_read(_z_unicast_transport_manager_t *manager,
                                             const _z_ready_links_mask_bitset_t *ready_links) {
    bool has_data = false;
    _z_address_to_unicast_transport_peer_hmap_iter_t id =
        _z_address_to_unicast_transport_peer_hmap_begin(&manager->_peers);
    while (id != _z_address_to_unicast_transport_peer_hmap_end(&manager->_peers)) {
        _z_address_to_unicast_transport_peer_hmap_iter_t next =
            _z_address_to_unicast_transport_peer_hmap_iter_next(&manager->_peers, id);
        if (!_z_ready_links_mask_bitset_is_unicast_peer_ready(ready_links, (size_t)id)) {
            id = next;
            continue;
        }
        z_result_t ret = _z_unicast_transport_peer_recv(manager, id);
        size_t peer_id = (size_t)id;
        if (ret == Z_NO_DATA) {
            id = next;
            continue;
        }
        if (ret == _Z_RES_OK) {
            has_data = true;
            ret = _z_unicast_transport_peer_process_messages(manager, id);
        }
        if (ret == _Z_RES_OK) {
            id = next;
            continue;
        }

        const _z_close_reason_t *reason = NULL;
        _z_close_reason_t generic_reason = _Z_CLOSE_REASON_GENERIC;
        if (ret == Z_REMOTE_PEER_SENT_CLOSE) {
            _Z_INFO("Remote peer %zu sent a CLOSE message", peer_id);
        } else {
            _Z_ERROR("Failed to receive or handle data from peer %zu (err: %d)", peer_id, ret);
            reason = &generic_reason;
        }
        ret = _z_unicast_transport_manager_close_peer(manager, id, reason, NULL);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Failed to close peer %zu (err: %d)", peer_id, ret);
        }
        id = next;
    }
    return has_data ? _Z_RES_OK : Z_NO_DATA;
}

#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
