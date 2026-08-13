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
#include "zenoh-pico/transport/multicast/rx.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/socket.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/definitions/network.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/system/common/platform.h"
#include "zenoh-pico/transport/multicast/connectivity.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/raweth/rx.h"
#include "zenoh-pico/transport/rx.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

static z_result_t _z_multicast_transport_manager_handle_join_existing_peer(
    _z_multicast_transport_manager_t *manager, const _z_t_msg_join_t *msg,
    _z_address_to_multicast_transport_peer_hmap_iter_t iter) {
    _z_multicast_transport_peer_t *peer = &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, iter)->val;

    const _z_multicast_transport_group_t *group =
        _z_multicast_transport_group_vec_const_at(&manager->_groups, peer->_multicast_group);
    if (!_z_multicast_group_check_peer_compatibility(group, msg)) {
        _Z_INFO("Couldn't accept peer on mcast group %zu because distant node is incompatible config wise.",
                (size_t)peer->_multicast_group);
        _Z_ERROR_RETURN(_Z_ERR_TRANSPORT_OPEN_SN_RESOLUTION);
    }
#if Z_FEATURE_FRAGMENTATION == 1
    peer->_sn_rx = msg->_next_sn;
    _z_sn_decrement_all(&peer->_sn_rx, manager->_sn_res);
#endif
    if (peer->_lease_duration_ms != msg->_lease) {
        peer->_lease_duration_ms = (uint32_t)msg->_lease;
        _ZP_REMOVE_ONE(_z_multicast_lease_pqueue, &manager->_lease_pqueue, *_ == iter);
        peer->_lease_deadline = z_clock_now();
        z_clock_advance_ms(&peer->_lease_deadline, (unsigned long)peer->_lease_duration_ms);
        _z_multicast_lease_pqueue_push(&manager->_lease_pqueue,
                                       &iter);  // can not fail since it was already in the queue
        _z_transport_manager_signal_opened_peer(manager->_parent, _ZP_CONNECT_PEER_ID_FROM_LISTEN);  // wake up tasks
    }

    return _Z_RES_OK;
}

static z_result_t _z_multicast_transport_manager_handle_transport_message(
    _z_multicast_transport_manager_t *manager, _z_transport_message_t *t_msg,
    _z_address_to_multicast_transport_peer_hmap_iter_t peer_iter) {
    _z_multicast_transport_peer_t *peer =
        &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, peer_iter)->val;
    peer->_received = true;
    switch (_Z_MID(t_msg->_header)) {
        case _Z_MID_T_FRAME:
            _Z_DEBUG("Received _Z_FRAME message");
            return _z_transport_handle_frame(manager->_parent, t_msg->_header, &t_msg->_body._frame,
#if Z_FEATURE_FRAGMENTATION == 1
                                             &peer->_dbuf, &peer->_sn_rx,
#endif
                                             (size_t)peer_iter + Z_MAX_NUM_UNICAST_PEERS);
        case _Z_MID_T_FRAGMENT: {
            _Z_DEBUG("Received Z_FRAGMENT message");
#if Z_FEATURE_FRAGMENTATION == 1
            return _z_transport_handle_fragment(manager->_parent, t_msg->_header, &t_msg->_body._fragment, &peer->_dbuf,
                                                &peer->_sn_rx, manager->_sn_res, peer->_patch,
                                                (size_t)peer_iter + Z_MAX_NUM_UNICAST_PEERS);
#else
            _Z_INFO("Received Z_FRAGMENT message but fragmentation feature is not enabled, dropping the message");
            return _Z_RES_OK;
#endif
        }
        case _Z_MID_T_KEEP_ALIVE: {
            _Z_DEBUG("Received _Z_KEEP_ALIVE message");
            return _Z_RES_OK;
        }

        case _Z_MID_T_INIT: {
            // Do nothing, multicast transports are not expected to handle INIT messages
            return _Z_RES_OK;  // should we rather return an error here and close the transport ?
        }

        case _Z_MID_T_OPEN: {
            // Do nothing, multicast transports are not expected to handle OPEN messages
            return _Z_RES_OK;  // should we rather return an error here and close the transport ?
        }

        case _Z_MID_T_JOIN: {
            _Z_DEBUG("Received _Z_JOIN message");
            return _z_multicast_transport_manager_handle_join_existing_peer(manager, &t_msg->_body._join, peer_iter);
        }

        case _Z_MID_T_CLOSE: {
            _Z_INFO("Closing connection as requested by the remote peer");
            return Z_REMOTE_PEER_SENT_CLOSE;
        }

        default: {
            _Z_INFO("WARNING: Unknown transport message ID");
            return _Z_RES_OK;  // should we rather return an error here and close the transport ?
        }
    }
}

static z_result_t _z_multicast_transport_group_handle_transport_message_from_unknown_peer(
    _z_multicast_transport_group_t *group, _z_transport_message_t *t_msg, const _z_link_address_t *src_addr) {
    switch (_Z_MID(t_msg->_header)) {
        case _Z_MID_T_JOIN:
            _Z_DEBUG("Received Z_JOIN message");
            z_result_t ret = _z_multicast_transport_group_add_peer(group, src_addr, &t_msg->_body._join);
            if (ret < _Z_RES_OK) {
                _Z_ERROR("Failed to add peer " _Z_ID_PRINT_FORMAT " on multicast group: %zu (error: %d)",
                         _Z_ID_PRINT_ARGS(&t_msg->_body._join._zid), _z_multicast_transport_group_id(group), ret);
            } else if (ret > _Z_RES_OK) {
                _Z_WARN("Ignoring join message from peer " _Z_ID_PRINT_FORMAT " on multicast group: %zu (%d)",
                        _Z_ID_PRINT_ARGS(&t_msg->_body._join._zid), _z_multicast_transport_group_id(group), ret);
            } else {
                _Z_INFO("Added new peer " _Z_ID_PRINT_FORMAT " on multicast group: %zu",
                        _Z_ID_PRINT_ARGS(&t_msg->_body._join._zid), _z_multicast_transport_group_id(group));
            }
            return _Z_RES_OK;
        default: {
            _Z_INFO("WARNING: Received non-join transport message %zu from unknown peer on mcast group %zu, ",
                    (size_t)_Z_MID(t_msg->_header), _z_multicast_transport_group_id(group));
            return _Z_RES_OK;
        }
    }
}

static z_result_t _z_multicast_link_recv_zbuf_stream(_z_multicast_link_t *link, _z_zbuf_t *zbf, uint16_t *msg_len,
                                                     _z_link_address_t *src_out) {
    size_t readable_len = _z_zbuf_readable_len(zbf);
    size_t part_len = *msg_len == 0 ? _Z_MSG_LEN_ENC_SIZE : *msg_len;
    size_t remaining_capacity = _z_zbuf_writable_space_left(zbf) + readable_len;
    if (remaining_capacity < part_len) {
        _z_zbuf_compact(zbf);
        remaining_capacity = _z_zbuf_writable_space_left(zbf) + readable_len;
        if (remaining_capacity < part_len) {
            _Z_ERROR_LOG(_Z_ERR_TRANSPORT_NO_SPACE);
            return _Z_ERR_TRANSPORT_NO_SPACE;
        }
    }
    if (_z_multicast_link_recv_zbuf(link, zbf, src_out)) {
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

static z_result_t _z_multicast_link_recv_zbuf_datagram(_z_multicast_link_t *link, _z_zbuf_t *zbf,
                                                       _z_link_address_t *src_out) {
    _z_zbuf_reset(zbf);
    if (_z_multicast_link_recv_zbuf(link, zbf, src_out)) {
#if Z_FEATURE_RAWETH_TRANSPORT == 1
        if (_z_multicast_link_inner_is_raweth(&link->_inner)) {
            _Z_RETURN_IF_ERR(_z_raweth_process_rx_buffer(zbf));
        }
#endif
        return _z_zbuf_readable_len(zbf) > 0 ? _Z_RES_OK : Z_NO_DATA;
    } else {
        return Z_NO_DATA;  // with current link implementation there is no way to distinguish between a read error and
                           // timeout
    }
}

static z_result_t _z_multicast_transport_group_recv(_z_multicast_transport_manager_t *manager, size_t group_id,
                                                    _z_link_address_t *src_out) {
    _z_link_address_init(src_out);  // _z_link_address_t is trivially destructible, so we can safely initialize it here
    _z_multicast_transport_group_t *group = _z_multicast_transport_group_vec_at(&manager->_groups, group_id);
    return _z_multicast_link_is_streamed(&group->_link)
               ? _z_multicast_link_recv_zbuf_stream(&group->_link, &group->_rx_buffer, &group->_rx_msg_len, src_out)
               : _z_multicast_link_recv_zbuf_datagram(&group->_link, &group->_rx_buffer, src_out);
}

static z_result_t _z_multicast_transport_group_process_messages(_z_multicast_transport_manager_t *manager,
                                                                size_t group_id, const _z_link_address_t *src_addr) {
    _z_multicast_transport_group_t *group = _z_multicast_transport_group_vec_at(&manager->_groups, group_id);
    _z_address_to_multicast_transport_peer_hmap_iter_t peer_iter =
        _z_address_to_multicast_transport_peer_hmap_get_iter(&manager->_peers, src_addr);
    bool has_more_messages = true;
    uint16_t *rx_msg_len_ptr = _z_multicast_link_is_streamed(&group->_link) ? &group->_rx_msg_len : NULL;
    while (has_more_messages) {
        _z_transport_message_t t_msg = {0};
        z_result_t ret = _z_transport_read_message(&group->_rx_buffer, rx_msg_len_ptr, &t_msg, &has_more_messages);
        if (peer_iter == _z_address_to_multicast_transport_peer_hmap_end(&manager->_peers)) {
            _Z_SET_IF_OK(
                ret, _z_multicast_transport_group_handle_transport_message_from_unknown_peer(group, &t_msg, src_addr));
            _Z_RETURN_IF_ERR(ret);
        } else {
            _Z_SET_IF_OK(ret, _z_multicast_transport_manager_handle_transport_message(manager, &t_msg, peer_iter));
            if (ret != _Z_RES_OK) {
                if (ret == Z_REMOTE_PEER_SENT_CLOSE) {
                    _Z_INFO("Remote peer %zu from group %zu sent a CLOSE message", (size_t)peer_iter,
                            _z_multicast_transport_group_id(group));
                    ret = _z_multicast_transport_manager_close_peer(manager, peer_iter, NULL, NULL);
                } else {
                    _Z_ERROR("Failed to handle transport message from peer %zu in group %zu (err: %d)",
                             (size_t)peer_iter, _z_multicast_transport_group_id(group), ret);
                    _z_close_reason_t reason = _Z_CLOSE_REASON_GENERIC;
                    ret = _z_multicast_transport_manager_close_peer(manager, peer_iter, &reason, NULL);
                }
                if (ret != _Z_RES_OK) {
                    _Z_ERROR("Failed to close peer %zu from group %zu (err: %d)", (size_t)peer_iter,
                             _z_multicast_transport_group_id(group), ret);
                }
                break;
            }
        }
    }
    return _Z_RES_OK;
}

z_result_t _z_multicast_transport_manager_read(_z_multicast_transport_manager_t *manager,
                                               const _z_ready_links_mask_bitset_t *ready_links) {
    bool has_data = false;
    for (size_t i = 0; i < _z_multicast_transport_group_vec_size(&manager->_groups); ++i) {
        if (!_z_ready_links_mask_bitset_is_multicast_group_ready(ready_links, i)) {
            continue;
        }
        _z_link_address_t src_addr;
        z_result_t ret = _z_multicast_transport_group_recv(manager, i, &src_addr);
        if (ret == Z_NO_DATA) {
            continue;
        } else if (ret != _Z_RES_OK) {
            _Z_ERROR("Failed recv from multicast group %zu (err: %d)", i, ret);
            continue;
        }
        has_data = true;
        ret = _z_multicast_transport_group_process_messages(manager, i, &src_addr);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Failed to process messages from multicast group %zu (err: %d)", i, ret);
            continue;
        }
    }
    return has_data ? _Z_RES_OK : Z_NO_DATA;
}

#endif  // Z_FEATURE_MULTICAST_TRANSPORT == 1
