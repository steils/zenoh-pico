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

#include "zenoh-pico/transport/rx.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/link/transport/socket.h"
#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/transport/fragmentation.h"
#include "zenoh-pico/transport/multicast/rx.h"
#include "zenoh-pico/transport/unicast/rx.h"
#include "zenoh-pico/transport/utils.h"

static z_result_t _z_transport_read_message_datagram(_z_zbuf_t *rx_buffer, _z_transport_message_t *t_msg,
                                                     bool *has_more_messages) {
    z_result_t ret = _z_transport_message_decode(t_msg, rx_buffer);
    *has_more_messages = _z_zbuf_readable_len(rx_buffer) > 0;
    return ret;
}

static z_result_t _z_transport_read_message_stream(_z_zbuf_t *rx_buffer, uint16_t *rx_msg_len,
                                                   _z_transport_message_t *t_msg, bool *has_more_messages) {
    *has_more_messages = false;
    _z_zbuf_t view = _z_zbuf_view(rx_buffer, *rx_msg_len);
    _Z_RETURN_IF_ERR(_z_transport_message_decode(t_msg, &view));
    _z_zbuf_set_rpos(rx_buffer, _z_zbuf_get_rpos(rx_buffer) + _z_zbuf_get_rpos(&view));
    if (_z_zbuf_readable_len(&view) > 0) {
        *rx_msg_len = (uint16_t)_z_zbuf_readable_len(&view);
        *has_more_messages = true;
    } else {
        *rx_msg_len = 0;  // Reset the message length for the next iteration
        if (_z_zbuf_readable_len(rx_buffer) >= _Z_MSG_LEN_ENC_SIZE) {
            *rx_msg_len = (uint16_t)_z_transport_read_stream_size(rx_buffer);
            *has_more_messages = _z_zbuf_readable_len(rx_buffer) >= *rx_msg_len;
        }
    }
    return _Z_RES_OK;
}

z_result_t _z_transport_read_message(_z_zbuf_t *rx_buffer, uint16_t *rx_msg_len, _z_transport_message_t *t_msg,
                                     bool *has_more_messages) {
    return rx_msg_len == NULL ? _z_transport_read_message_datagram(rx_buffer, t_msg, has_more_messages)
                              : _z_transport_read_message_stream(rx_buffer, rx_msg_len, t_msg, has_more_messages);
}

uint16_t _z_transport_read_stream_size(_z_zbuf_t *zbuf) {
    uint16_t len = 0;
    for (uint8_t i = 0; i < _Z_MSG_LEN_ENC_SIZE; i++) {
        len |= (uint16_t)(_z_zbuf_read(zbuf) << (i * (uint8_t)8));
    }
    return len;
}

z_result_t _z_transport_handle_frame(_z_transport_manager_t *manager, uint8_t header, const _z_t_msg_frame_t *frame,
#if Z_FEATURE_FRAGMENTATION == 1
                                     _z_dbuf_t *dbuf, _z_sn_t *sn_rx,
#endif
                                     size_t peer) {
    z_reliability_t tmsg_reliability =
        _Z_HAS_FLAG(header, _Z_FLAG_T_FRAME_R) ? Z_RELIABILITY_RELIABLE : Z_RELIABILITY_BEST_EFFORT;
#if Z_FEATURE_FRAGMENTATION == 1
    _z_dbuf_reset(dbuf, tmsg_reliability);
    *_z_sn_get(sn_rx, tmsg_reliability) = frame->_sn;
#endif
    // Handle all the zenoh messages, one by one
    _z_network_message_t curr_nmsg = {0};
    _z_zbuf_t buf = _z_slice_as_zbuf(_z_slice_view_deref(&frame->_payload));
    while (_z_zbuf_readable_len(&buf) > 0) {
        _Z_RETURN_IF_ERR(_z_network_message_decode(&curr_nmsg, &buf));
        curr_nmsg._reliability = tmsg_reliability;
        _Z_RETURN_IF_ERR(_z_handle_network_message(manager->_session, &curr_nmsg, peer));
    }
    return _Z_RES_OK;
}

#if Z_FEATURE_FRAGMENTATION == 1
z_result_t _z_transport_handle_fragment(_z_transport_manager_t *manager, uint8_t header, _z_t_msg_fragment_t *msg,
                                        _z_dbuf_t *dbuf, _z_sn_t *sn_rx, _z_zint_t sn_res, uint8_t patch,
                                        size_t peer_iter) {
    z_reliability_t tmsg_reliability =
        _Z_HAS_FLAG(header, _Z_FLAG_T_FRAGMENT_R) ? Z_RELIABILITY_RELIABLE : Z_RELIABILITY_BEST_EFFORT;

    _z_zint_t *sn = _z_sn_get(sn_rx, tmsg_reliability);
    _z_wbuf_t *buf = _z_dbuf_get(dbuf, tmsg_reliability);

    _z_zint_t sn_old = *sn;
    *sn = msg->_sn;
    // Only enforce SN consecutiveness while a defragmentation is already in progress. When the buffer is empty this
    // fragment starts a fresh message, so its SN just resynchronizes the receiver (e.g. the first fragmented message
    // of a given reliability, whose SN was never advanced by a preceding frame).
    if (_z_wbuf_len(buf) > 0 && !_z_sn_consecutive(sn_res, sn_old, msg->_sn)) {
        _z_dbuf_reset(dbuf, tmsg_reliability);
        _Z_INFO("Defragmentation buffer for reliability %d dropped because non-consecutive fragments received",
                tmsg_reliability);
        return _Z_RES_OK;
    }

    if (_Z_PATCH_HAS_FRAGMENT_MARKERS(patch)) {
        if (msg->first) {
            _z_wbuf_reset(buf);
        } else if (_z_wbuf_len(buf) == 0) {
            _Z_INFO("First fragment received without the start marker");
            return _Z_RES_OK;
        }
        if (msg->drop) {
            _z_dbuf_reset(dbuf, tmsg_reliability);
            return _Z_RES_OK;
        }
    }

    const _z_slice_t *payload_slice = _z_slice_view_deref(&msg->_payload);
    if (_z_wbuf_space_left(buf) < payload_slice->len) {
        if (_Z_PATCH_HAS_FRAGMENT_MARKERS(patch)) {
            _z_dbuf_overflow(dbuf, tmsg_reliability);
        } else {
            _z_dbuf_reset(dbuf, tmsg_reliability);
        }
        _Z_INFO("Fragment dropped because defragmentation buffer has overflown");
        return _Z_RES_OK;
    } else {
        _z_wbuf_write_bytes(buf, payload_slice->start, 0, payload_slice->len);
    }

    z_result_t ret = _Z_RES_OK;
    // Process final fragment
    if (!_Z_HAS_FLAG(header, _Z_FLAG_T_FRAGMENT_M)) {
        _z_zbuf_t zbf = _z_wbuf_as_zbuf_view(buf);
        // Decode message
        _z_network_message_t zm = {0};
        ret = _z_network_message_decode(&zm, &zbf);
        if (ret == _Z_RES_OK) {
            zm._reliability = tmsg_reliability;
            _z_handle_network_message(manager->_session, &zm, peer_iter);
        } else {
            _Z_INFO("Failed to decode defragmented message");
            _Z_ERROR_LOG(_Z_ERR_MESSAGE_DESERIALIZATION_FAILED);
            ret = _Z_ERR_MESSAGE_DESERIALIZATION_FAILED;
        }
        _z_dbuf_reset(dbuf, tmsg_reliability);
    }
    return ret;
}
#endif

z_result_t _z_transport_manager_read(_z_transport_manager_t *manager) {
    bool has_data = false;
    z_result_t ret = _Z_RES_OK;
    _z_ready_links_mask_bitset_t ready_links = _z_transport_manager_wait_readable(manager);
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    ret = _z_unicast_transport_manager_read(&manager->_unicast, &ready_links);
    if (ret == _Z_RES_OK) {
        has_data = true;
    } else if (ret != Z_NO_DATA) {
        _Z_ERROR("Failed to read from unicast transport manager: %d", ret);
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    ret = _z_multicast_transport_manager_read(&manager->_multicast, &ready_links);
    if (ret == _Z_RES_OK) {
        has_data = true;
    } else if (ret != Z_NO_DATA) {
        _Z_ERROR("Failed to read from multicast transport manager: %d", ret);
    }
#endif
    return has_data ? _Z_RES_OK : Z_NO_DATA;
}

typedef struct _z_transport_manager_wait_iter_context_t {
    _z_transport_manager_t *manager;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_address_to_unicast_transport_peer_hmap_iter_t unicast_peer_iter;
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    size_t multicast_group_iter;
#endif
    _z_ready_links_mask_bitset_t ready_links_mask;
} _z_transport_manager_wait_iter_context_t;

static bool _z_transport_manager_wait_iter_find_first_non_null(_z_transport_manager_wait_iter_context_t *ctx) {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_address_to_unicast_transport_peer_hmap_t *peers = &ctx->manager->_unicast._peers;
    for (; ctx->unicast_peer_iter != _z_address_to_unicast_transport_peer_hmap_end(peers);
         ctx->unicast_peer_iter = _z_address_to_unicast_transport_peer_hmap_iter_next(peers, ctx->unicast_peer_iter)) {
        _z_unicast_transport_peer_t *peer =
            &_z_address_to_unicast_transport_peer_hmap_at(peers, ctx->unicast_peer_iter)->val;
        if (_z_unicast_link_get_socket(&peer->_link) != NULL) {
            return true;
        } else {
            _z_ready_links_mask_bitset_set(&ctx->ready_links_mask, ctx->unicast_peer_iter,
                                           true);  // set the link as ready to force read on it
        }
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    size_t m_end = _z_multicast_transport_group_vec_size(&ctx->manager->_multicast._groups);
    _ZP_IT_FIND(_z_multicast_transport_group_vec, &ctx->manager->_multicast._groups, ctx->multicast_group_iter, m_end,
                _z_multicast_link_get_socket(&_->_link) != NULL);
    if (ctx->multicast_group_iter != m_end) {
        return true;
    }
#endif
    return false;
}

bool _z_transport_manager_wait_iter_reset(_z_socket_wait_iter_t *iter) {
    _z_transport_manager_wait_iter_context_t *ctx = (_z_transport_manager_wait_iter_context_t *)iter->_ctx;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    ctx->unicast_peer_iter = _z_address_to_unicast_transport_peer_hmap_begin(&ctx->manager->_unicast._peers);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    ctx->multicast_group_iter = 0;
#endif
    return _z_transport_manager_wait_iter_find_first_non_null(ctx);
}

bool _z_transport_manager_wait_iter_next(_z_socket_wait_iter_t *iter) {
    _z_transport_manager_wait_iter_context_t *ctx = (_z_transport_manager_wait_iter_context_t *)iter->_ctx;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    if (ctx->unicast_peer_iter != _z_address_to_unicast_transport_peer_hmap_end(&ctx->manager->_unicast._peers)) {
        ctx->unicast_peer_iter =
            _z_address_to_unicast_transport_peer_hmap_iter_next(&ctx->manager->_unicast._peers, ctx->unicast_peer_iter);
        if (_z_transport_manager_wait_iter_find_first_non_null(ctx)) {
            return true;
        }
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    size_t m_end = _z_multicast_transport_group_vec_size(&ctx->manager->_multicast._groups);
    if (ctx->multicast_group_iter < m_end) {
        ctx->multicast_group_iter++;
        return _z_transport_manager_wait_iter_find_first_non_null(ctx);
    }
#endif
    return false;
}

const _z_sys_net_socket_t *_z_transport_manager_wait_iter_get_socket(const _z_socket_wait_iter_t *iter) {
    _z_transport_manager_wait_iter_context_t *ctx = (_z_transport_manager_wait_iter_context_t *)iter->_ctx;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    if (ctx->unicast_peer_iter != _z_address_to_unicast_transport_peer_hmap_end(&ctx->manager->_unicast._peers)) {
        return _z_unicast_link_get_socket(
            &_z_address_to_unicast_transport_peer_hmap_at(&ctx->manager->_unicast._peers, ctx->unicast_peer_iter)
                 ->val._link);
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    if (ctx->multicast_group_iter < _z_multicast_transport_group_vec_size(&ctx->manager->_multicast._groups)) {
        _z_multicast_transport_group_t *group =
            _z_multicast_transport_group_vec_get(&ctx->manager->_multicast._groups, ctx->multicast_group_iter);
        return _z_multicast_link_get_socket(&group->_link);
    }
#endif
    return NULL;
}

void _z_transport_manager_wait_iter_set_ready_f(_z_socket_wait_iter_t *iter, bool ready) {
    _z_transport_manager_wait_iter_context_t *ctx = (_z_transport_manager_wait_iter_context_t *)iter->_ctx;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    if (ctx->unicast_peer_iter != _z_address_to_unicast_transport_peer_hmap_end(&ctx->manager->_unicast._peers)) {
        _z_ready_links_mask_bitset_set(&ctx->ready_links_mask, ctx->unicast_peer_iter, ready);
        return;
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    if (ctx->multicast_group_iter < _z_multicast_transport_group_vec_size(&ctx->manager->_multicast._groups)) {
        _z_ready_links_mask_bitset_set(&ctx->ready_links_mask,
                                       (size_t)(ctx->multicast_group_iter + Z_MAX_NUM_UNICAST_PEERS), ready);
        return;
    }
#endif
}

_z_ready_links_mask_bitset_t _z_transport_manager_wait_readable(_z_transport_manager_t *manager) {
    _z_transport_manager_wait_iter_context_t ctx;
    ctx.manager = manager;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    ctx.unicast_peer_iter = _z_address_to_unicast_transport_peer_hmap_begin(&manager->_unicast._peers);
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    ctx.multicast_group_iter = 0;
#endif
    ctx.ready_links_mask = _z_ready_links_mask_bitset_new();
    // Set all bits to true to ensure that we will check all sockets that do not support readiness detection.
    _z_ready_links_mask_bitset_set_all(&ctx.ready_links_mask, true);
    _z_socket_wait_iter_t iter;
    iter._ctx = &ctx;
    iter._reset = _z_transport_manager_wait_iter_reset;
    iter._next = _z_transport_manager_wait_iter_next;
    iter._get_socket = _z_transport_manager_wait_iter_get_socket;
    iter._set_ready = _z_transport_manager_wait_iter_set_ready_f;
    if (_z_socket_wait_readable(&iter, Z_CONFIG_SOCKET_TIMEOUT) < _Z_RES_OK) {
        _z_ready_links_mask_bitset_set_all(&ctx.ready_links_mask, true);  // force read on all sockets
    }
    return ctx.ready_links_mask;
}

#undef _ZP_TRANSPORT_READINESS_MAX_LINKS
