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
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1

static z_result_t _z_unicast_transport_manager_handle_transport_message(
    _z_unicast_transport_manager_t *manager, _z_transport_message_t *t_msg,
    _z_unicast_transport_peer_hset_iter_t peer_iter) {
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_hset_at(&manager->_peers, peer_iter);
    peer->_received = true;  // mark that we have received a message from this peer
    switch (_Z_MID(t_msg->_header)) {
        case _Z_MID_T_FRAME:
            _Z_DEBUG("Received Z_FRAME message");
            return _z_transport_handle_frame(manager->_parent, t_msg->_header, &t_msg->_body._frame,
#if Z_FEATURE_FRAGMENTATION == 1
                                             &peer->_dbuf, &peer->_sn_rx,
#endif
                                             (size_t)peer_iter);
        case _Z_MID_T_FRAGMENT:
            _Z_DEBUG("Received Z_FRAGMENT message");
#if Z_FEATURE_FRAGMENTATION == 1
            return _z_transport_handle_fragment(manager->_parent, t_msg->_header, &t_msg->_body._fragment, &peer->_dbuf,
                                                &peer->_sn_rx, peer->_sn_res, peer->_patch, (size_t)peer_iter);
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
                                                 _z_unicast_transport_peer_hset_iter_t peer_iter) {
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_hset_at(&manager->_peers, peer_iter);

    return _z_unicast_link_is_streamed(&peer->_link)
               ? _z_unicast_link_recv_zbuf_stream(&peer->_link, &peer->_rx_buffer, &peer->_rx_msg_len)
               : _z_unicast_link_recv_zbuf_datagram(&peer->_link, &manager->_rx_buffer);
}

static z_result_t _z_unicast_transport_peer_process_messages(_z_unicast_transport_manager_t *manager,
                                                             _z_unicast_transport_peer_hset_iter_t peer_iter) {
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_hset_at(&manager->_peers, peer_iter);
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
        _Z_RETURN_IF_ERR(_z_unicast_transport_manager_handle_transport_message(manager, &t_msg, peer_iter));
    }
    return _Z_RES_OK;
}

z_result_t _z_unicast_transport_manager_read(_z_unicast_transport_manager_t *manager,
                                             const _z_ready_links_mask_bitset_t *ready_links) {
    bool has_data = false;
    _z_unicast_transport_peer_hset_iter_t iter = _z_unicast_transport_peer_hset_begin(&manager->_peers);
    while (iter != _z_unicast_transport_peer_hset_end(&manager->_peers)) {
        if (!_z_ready_links_mask_bitset_is_unicast_peer_ready(ready_links, (size_t)iter)) {
            iter = _z_unicast_transport_peer_hset_iter_next(&manager->_peers, iter);
            continue;
        }
        z_result_t ret = _z_unicast_transport_peer_recv(manager, iter);
        size_t peer_id = (size_t)iter;
        if (ret == Z_NO_DATA) {
            iter = _z_unicast_transport_peer_hset_iter_next(&manager->_peers, iter);
        } else if (ret != _Z_RES_OK) {
            _Z_ERROR("Failed to receive data from peer %zu (err: %d)", (size_t)iter, ret);
            _z_close_reason_t reason = _Z_CLOSE_REASON_GENERIC;
            ret = _z_unicast_transport_manager_close_peer(manager, iter, &reason, &iter);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to close peer %zu (err: %d)", peer_id, ret);
            }
        } else {
            has_data = true;
            ret = _z_unicast_transport_peer_process_messages(manager, iter);
            if (ret != _Z_RES_OK) {
                if (ret == Z_REMOTE_PEER_SENT_CLOSE) {
                    _Z_INFO("Remote peer %zu sent a CLOSE message", peer_id);
                    ret = _z_unicast_transport_manager_close_peer(manager, iter, NULL, &iter);
                } else {
                    _Z_ERROR("Failed to handle transport message from peer %zu (err: %d)", peer_id, ret);
                    _z_close_reason_t reason = _Z_CLOSE_REASON_GENERIC;
                    ret = _z_unicast_transport_manager_close_peer(manager, iter, &reason, &iter);
                }
                if (ret != _Z_RES_OK) {
                    _Z_ERROR("Failed to close peer %zu (err: %d)", peer_id, ret);
                }
            } else {
                iter = _z_unicast_transport_peer_hset_iter_next(&manager->_peers, iter);
            }
        }
    }
    return has_data ? _Z_RES_OK : Z_NO_DATA;
}

#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
