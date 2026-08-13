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

#include "zenoh-pico/transport/multicast/tx.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/transport/multicast/group.h"
#include "zenoh-pico/transport/raweth/tx.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/tx.h"
#include "zenoh-pico/transport/utils.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

static void _z_multicast_wbuf_prepare(_z_wbuf_t *wbf, _z_multicast_link_t *link, const _z_network_message_t *n_msg) {
#if Z_FEATURE_RAWETH_TRANSPORT == 1
    if (_z_multicast_link_inner_is_raweth(&link->_inner)) {
        _z_multicast_link_raweth_t *raweth = _z_multicast_link_inner_get_raweth(&link->_inner);
        _z_raweth_wbf_and_link_prepare(wbf, raweth, n_msg);
        return;
    }
#endif
    _ZP_UNUSED(n_msg);
    _z_wbuf_prepare(wbf, _z_multicast_link_is_streamed(link));
}

static void _z_multicast_wbuf_finalize(_z_wbuf_t *wbf, _z_multicast_link_t *link) {
#if Z_FEATURE_RAWETH_TRANSPORT == 1
    if (_z_multicast_link_inner_is_raweth(&link->_inner)) {
        _z_multicast_link_raweth_t *raweth = _z_multicast_link_inner_get_raweth(&link->_inner);
        _z_raweth_wbuf_and_link_finalize(wbf, raweth);
        return;
    }
#endif
    _z_wbuf_finalize(wbf, _z_multicast_link_is_streamed(link));
}

static z_result_t _z_multicast_transport_group_flush_buffer(_z_multicast_transport_group_t *group, _z_wbuf_t *wbuf) {
    _z_multicast_wbuf_finalize(wbuf, &group->_link);
    z_result_t ret = _Z_RES_OK;
    if (!_z_multicast_link_send_wbuf(&group->_link, wbuf)) {
        ret = _Z_ERR_TRANSPORT_TX_FAILED;
        _Z_ERROR_LOG(ret);
    } else {
        group->_transmitted = true;
    }
    _z_wbuf_reset(wbuf);
    return ret;
}

#if Z_FEATURE_FRAGMENTATION == 1
static z_result_t _z_multicast_transport_group_send_fragment(_z_multicast_transport_group_t *group, _z_wbuf_t *tx_buf,
                                                             const _z_network_message_t *n_msg,
                                                             z_reliability_t reliability) {
    _z_wbuf_t frag_buff;
    _Z_RETURN_IF_ERR(_z_wbuf_init(&frag_buff, _Z_FRAG_BUFF_BASE_SIZE, true));

    bool is_first = true;
    // Encode message on temp buffer
    _Z_CLEAN_RETURN_IF_ERR(_z_network_message_encode(&frag_buff, n_msg), _z_wbuf_clear(&frag_buff);
                           _z_wbuf_reset(tx_buf));
    // Fragment message
    z_result_t ret = _Z_RES_OK;
    while (_z_wbuf_len(&frag_buff) > 0) {
        // Serialize fragment
        _z_multicast_wbuf_prepare(tx_buf, &group->_link, n_msg);
        _z_zint_t *sn = _z_sn_get(&group->_sn_tx, reliability);
        ret = _z_wbuf_serialize_zenoh_fragment(tx_buf, &frag_buff, reliability, *sn, is_first);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Fragment serialization failed with err %d", ret);
            break;
        }
        _z_sn_increment(sn, group->_parent->_sn_res);
        ret = _z_multicast_transport_group_flush_buffer(group, tx_buf);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Fragment send failed with err %d", ret);
            break;
        }
        is_first = false;
    }
    _z_wbuf_clear(&frag_buff);
    _z_wbuf_reset(tx_buf);
    return ret;
}

#endif

#if Z_FEATURE_BATCHING == 1
static bool _z_multicast_transport_is_link_raweth(const _z_multicast_link_t *link) {
#if Z_FEATURE_RAWETH_TRANSPORT == 1
    return _z_multicast_link_inner_is_raweth(&link->_inner);
#else
    _ZP_UNUSED(link);
    return false;
#endif
}
#endif

z_result_t _z_multicast_transport_manager_send_n_msg_to_group(_z_multicast_transport_manager_t *manager,
                                                              const _z_network_message_t *msg,
                                                              z_reliability_t reliability, size_t group_id) {
    _Z_DEBUG("Send network message over multicast to group %zu", group_id);
    _z_multicast_transport_group_t *group = _z_multicast_transport_group_vec_at(&manager->_groups, group_id);
    _z_zint_t *sn = _z_sn_get(&group->_sn_tx, reliability);
#if Z_FEATURE_BATCHING == 0
    _z_wbuf_t *tx_buf = &manager->_parent->_tx_buffer;
    bool new_frame = true;
    // FIXME: temporary reduce the size of the buffer to that of peer MTU, if it is smaller than the default buffer size
#else
    _z_wbuf_t *tx_buf = &group->_tx_buffer;
    bool new_frame = _z_wbuf_len(tx_buf) == 0;
    if (!new_frame && group->_is_current_tx_frame_reliable != (reliability == Z_RELIABILITY_RELIABLE)) {
        // If the reliability of the current frame being built in the batch is different from the reliability of the
        // message to be sent, flush the current frame and start a new one.
        _Z_CLEAN_RETURN_IF_ERR(_z_multicast_transport_group_flush_buffer(group, tx_buf), _z_wbuf_reset(tx_buf));
        new_frame = true;
    }
    group->_is_current_tx_frame_reliable = (reliability == Z_RELIABILITY_RELIABLE);
    size_t prev_wpos = _z_wbuf_get_wpos(tx_buf);
#endif
    if (new_frame) {
        _z_multicast_wbuf_prepare(tx_buf, &group->_link, msg);
        _z_transport_message_t t_msg = _z_t_msg_make_frame_header(*sn, reliability);
        _Z_CLEAN_RETURN_IF_ERR(_z_transport_message_encode(tx_buf, &t_msg), _z_wbuf_reset(tx_buf));
    }
    z_result_t ret = _z_network_message_encode(tx_buf, msg);
    if (ret == _Z_RES_OK) {
        if (new_frame) {
            _z_sn_increment(sn, manager->_sn_res);
        }
#if Z_FEATURE_BATCHING == 1
        if (manager->_parent->_batching_enabled && !_z_multicast_transport_is_link_raweth(&group->_link) &&
            !_z_network_message_get_express_status(msg)) {
            return _Z_RES_OK;
        }
#endif
        return _z_multicast_transport_group_flush_buffer(group, tx_buf);
#if Z_FEATURE_BATCHING == 1
    } else if (!new_frame) {
        // reset the buffer to previous state, flush it and retry sending again on an empty one
        _z_wbuf_set_wpos(tx_buf, prev_wpos);
        _z_multicast_transport_group_flush_buffer(group, tx_buf);
        return _z_multicast_transport_manager_send_n_msg_to_group(manager, msg, reliability, group_id);
#endif
    } else {
        _z_wbuf_reset(tx_buf);
    }
#if Z_FEATURE_FRAGMENTATION == 0
    return ret;
#else
    return _z_multicast_transport_group_send_fragment(group, tx_buf, msg, reliability);
#endif
}

static _z_peer_mask_bitset_iter_t _z_peer_bitmask_multicast_peer_begin(const _z_peer_mask_bitset_t *peers) {
#if Z_MAX_NUM_UNICAST_PEERS == 0
    return _z_peer_mask_bitset_begin_true(peers);
#else
    _z_peer_mask_bitset_iter_t iter = (_z_peer_mask_bitset_iter_t)(Z_MAX_NUM_UNICAST_PEERS - 1);
    return _z_peer_mask_bitset_iter_next_true(peers, iter);
#endif
}

z_result_t _z_multicast_transport_manager_send_n_msg(_z_multicast_transport_manager_t *manager,
                                                     const _z_network_message_t *msg, z_reliability_t reliability,
                                                     _z_destination_filter_t filter) {
    if (_z_destination_filter_is_what(&filter) &&
        ((*_z_destination_filter_get_what(&filter) & Z_WHAT_ROUTER_PEER) != 0)) {
        _ZP_IT_FOREACH (_z_multicast_transport_group_vec, &manager->_groups, iter) {
            z_result_t ret =
                _z_multicast_transport_manager_send_n_msg_to_group(manager, msg, reliability, (size_t)iter);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to send message to group %zu with err %d", (size_t)iter, ret);
            }
        }
    } else if (_z_destination_filter_is_mask(&filter)) {
        _z_peer_mask_bitset_t *mask = _z_destination_filter_get_mask(&filter);
        _z_mcast_group_mask_bitset_t group_mask = _z_mcast_group_mask_bitset_new();
        for (_z_peer_mask_bitset_iter_t iter = _z_peer_bitmask_multicast_peer_begin(mask);
             iter != _z_peer_mask_bitset_end(mask); iter = _z_peer_mask_bitset_iter_next_true(mask, iter)) {
            _z_address_to_multicast_transport_peer_hmap_iter_t peer_iter =
                (_z_address_to_multicast_transport_peer_hmap_iter_t)(iter - Z_MAX_NUM_UNICAST_PEERS);
            const _z_multicast_transport_peer_t *peer =
                &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, peer_iter)->val;
            if (*_z_mcast_group_mask_bitset_const_at(&group_mask, (size_t)peer->_multicast_group)) {
                continue;
            }
            _z_mcast_group_mask_bitset_set_at(&group_mask, (size_t)peer->_multicast_group, true);
            z_result_t ret = _z_multicast_transport_manager_send_n_msg_to_group(manager, msg, reliability,
                                                                                (size_t)peer->_multicast_group);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to send message to group %zu with err %d", (size_t)peer->_multicast_group, ret);
            }
        }
    } else if (_z_destination_filter_is_single(&filter)) {
        size_t peer_id = (size_t)*_z_destination_filter_get_single(&filter);
        if (peer_id >= Z_MAX_NUM_UNICAST_PEERS) {
            _z_address_to_multicast_transport_peer_hmap_iter_t peer_iter =
                (_z_address_to_multicast_transport_peer_hmap_iter_t)(peer_id - Z_MAX_NUM_UNICAST_PEERS);
            const _z_multicast_transport_peer_t *peer =
                &_z_address_to_multicast_transport_peer_hmap_at(&manager->_peers, peer_iter)->val;
            z_result_t ret = _z_multicast_transport_manager_send_n_msg_to_group(manager, msg, reliability,
                                                                                (size_t)peer->_multicast_group);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to send message to group %zu with err %d", (size_t)peer->_multicast_group, ret);
            }
        }
    }
    return _Z_RES_OK;
}

z_result_t _z_multicast_transport_manager_send_t_msg_to_group(_z_multicast_transport_manager_t *manager,
                                                              const _z_transport_message_t *t_msg, size_t group_id) {
    _z_multicast_transport_group_t *group = _z_multicast_transport_group_vec_at(&manager->_groups, group_id);
    _Z_DEBUG("Send transport message on transport multicast");
#if Z_FEATURE_BATCHING == 0
    _z_wbuf_t *tx_buf = &manager->_parent->_tx_buffer;
    // FIXME: temporary reduce the size of the buffer to that of peer MTU, if it is smaller than the default buffer size
#else
    _z_wbuf_t *tx_buf = &group->_tx_buffer;
    if (_z_wbuf_len(tx_buf) > 0) {
        _Z_RETURN_IF_ERR(_z_multicast_transport_group_flush_buffer(group, tx_buf));
    }
#endif
    _z_multicast_wbuf_prepare(tx_buf, &group->_link, NULL);
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_message_encode(tx_buf, t_msg), _z_wbuf_reset(tx_buf));
    return _z_multicast_transport_group_flush_buffer(group, tx_buf);
}

z_result_t _z_multicast_transport_manager_send_join(_z_multicast_transport_manager_t *manager) {
    // Note: we need to lock transport manager to ensure no other thread is sending messages while we are sending the
    // join message to all groups.
    z_result_t ret = _z_transport_manager_lock(manager->_parent);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("Failed to lock transport manager for send join %d", ret);
        return ret;
    }
    for (size_t i = 0; i < _z_multicast_transport_group_vec_size(&manager->_groups); ++i) {
        const _z_multicast_transport_group_t *group = _z_multicast_transport_group_vec_const_at(&manager->_groups, i);
        _z_transport_message_t msg = _z_t_msg_make_join(manager->_parent->_session->_mode, Z_TRANSPORT_LEASE,
                                                        manager->_parent->_session->_local_zid, group->_sn_tx,
                                                        _z_multicast_transport_group_get_mtu(group));
        _z_multicast_transport_manager_send_t_msg_to_group(manager, &msg, i);
        // TODO: handle the result of sending the join message - i.e reconnect listener if failed
    }
    _z_transport_manager_unlock(manager->_parent);
    return ret;
}

#if Z_FEATURE_BATCHING == 1
z_result_t _z_multicast_transport_manager_send_n_batch(_z_multicast_transport_manager_t *manager) {
    z_result_t res = _Z_RES_OK;
    _z_multicast_transport_group_t *group = NULL;
    _ZP_FOREACH (_z_multicast_transport_group_vec, &manager->_groups, group) {
        if (_z_wbuf_len(&group->_tx_buffer) > 0) {
            z_result_t ret = _z_multicast_transport_group_flush_buffer(group, &group->_tx_buffer);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to flush group %zu buffer with err %d", (size_t)_z_multicast_transport_group_id(group),
                         ret);
                res = ret;
            }
        }
    }
    return res;
}
#endif

#endif  // Z_FEATURE_MULTICAST_TRANSPORT == 1
