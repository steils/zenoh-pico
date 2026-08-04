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

#include "zenoh-pico/transport/unicast/tx.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/tx.h"
#include "zenoh-pico/transport/unicast/peer.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/transport/utils.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1
z_result_t _z_unicast_link_send_t_msg(_z_unicast_link_t *link, const _z_transport_message_t *t_msg) {
    z_result_t ret = _Z_RES_OK;
    uint16_t mtu = _z_unicast_link_get_mtu(link);
    mtu = (mtu < Z_BATCH_UNICAST_SIZE) ? mtu : Z_BATCH_UNICAST_SIZE;
    // Create and prepare the buffer to serialize the message on
    _z_wbuf_t wbf;
    _Z_RETURN_IF_ERR(_z_wbuf_init(&wbf, mtu, false));

    _z_wbuf_prepare(&wbf, _z_unicast_link_is_streamed(link));
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_message_encode(&wbf, t_msg), _z_wbuf_clear(&wbf));
    _z_wbuf_finalize(&wbf, _z_unicast_link_is_streamed(link));

    if (!_z_unicast_link_send_wbuf(link, &wbf)) {
        _Z_ERROR_LOG(_Z_ERR_TRANSPORT_TX_FAILED);
        ret = _Z_ERR_TRANSPORT_TX_FAILED;
    }
    _z_wbuf_clear(&wbf);

    return ret;
}

static _z_unicast_transport_peer_t *_z_unicast_transport_manager_get_established_peer(
    _z_unicast_transport_manager_t *manager, size_t peer_id) {
    if (peer_id >= Z_MAX_NUM_UNICAST_PEERS) {
        return NULL;
    }
    _z_unicast_peer_slot_id_t slot_id = (_z_unicast_peer_slot_id_t)peer_id;
    return manager->_peer_states[slot_id] == _Z_UNICAST_SLOT_ESTABLISHED
               ? _z_unicast_transport_peer_at(&manager->_peers, slot_id)
               : NULL;
}

static z_result_t _z_unicast_transport_peer_flush_buffer(_z_unicast_link_t *link, _z_unicast_transport_peer_t *peer,
                                                         _z_wbuf_t *wbuf) {
    _z_wbuf_finalize(wbuf, _z_unicast_link_is_streamed(link));
    z_result_t ret = _Z_RES_OK;
    if (!_z_unicast_link_send_wbuf(link, wbuf)) {
        ret = _Z_ERR_TRANSPORT_TX_FAILED;
        _Z_ERROR_LOG(ret);
    } else {
        peer->_transmitted = true;
    }
    _z_wbuf_reset(wbuf);
    return ret;
}

#if Z_FEATURE_FRAGMENTATION == 1
static z_result_t _z_unicast_transport_peer_send_fragment(_z_unicast_link_t *link, _z_unicast_transport_peer_t *peer,
                                                          _z_wbuf_t *tx_buf, const _z_network_message_t *n_msg,
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
        _z_wbuf_prepare(tx_buf, _z_unicast_link_is_streamed(link));
        _z_zint_t *sn = _z_sn_get(&peer->_sn_tx, reliability);
        ret = _z_wbuf_serialize_zenoh_fragment(tx_buf, &frag_buff, reliability, *sn, is_first);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Fragment serialization failed with err %d", ret);
            break;
        }
        _z_sn_increment(sn, peer->_sn_res);
        ret = _z_unicast_transport_peer_flush_buffer(link, peer, tx_buf);
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

z_result_t _z_unicast_transport_manager_send_n_msg_to_peer(_z_unicast_transport_manager_t *manager,
                                                           const _z_network_message_t *msg, z_reliability_t reliability,
                                                           size_t peer_id) {
    _Z_DEBUG("Send network message (%d) over unicast to peer %zu", msg->_tag, peer_id);
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_manager_get_established_peer(manager, peer_id);
    if (peer == NULL) {
        return _Z_ERR_INVALID;
    }
    _z_zint_t *sn = _z_sn_get(&peer->_sn_tx, reliability);
#if Z_FEATURE_BATCHING == 0
    _z_wbuf_t *tx_buf = &manager->_parent->_tx_buffer;
    bool new_frame = true;
    // FIXME: temporary reduce the size of the buffer to that of peer MTU, if it is smaller than the default buffer size
#else
    _z_wbuf_t *tx_buf = &peer->_tx_buffer;
    bool new_frame = _z_wbuf_len(tx_buf) == 0;
    if (!new_frame && peer->_is_current_tx_frame_reliable != (reliability == Z_RELIABILITY_RELIABLE)) {
        // If the reliability of the current frame being built in the batch is different from the reliability of the
        // message to be sent, flush the current frame and start a new one.
        _Z_CLEAN_RETURN_IF_ERR(_z_unicast_transport_peer_flush_buffer(&peer->_link, peer, tx_buf),
                               _z_wbuf_reset(tx_buf));
        new_frame = true;
    }
    peer->_is_current_tx_frame_reliable = (reliability == Z_RELIABILITY_RELIABLE);
    size_t prev_wpos = _z_wbuf_get_wpos(tx_buf);
#endif
    if (new_frame) {
        _z_wbuf_prepare(tx_buf, _z_unicast_link_is_streamed(&peer->_link));
        _z_transport_message_t t_msg = _z_t_msg_make_frame_header(*sn, reliability);
        _Z_RETURN_IF_ERR(_z_transport_message_encode(tx_buf, &t_msg));
    }
    z_result_t ret = _z_network_message_encode(tx_buf, msg);
    if (ret == _Z_RES_OK) {
        if (new_frame) {
            _z_sn_increment(sn, peer->_sn_res);
        }
#if Z_FEATURE_BATCHING == 1
        if (manager->_parent->_batching_enabled && !_z_network_message_get_express_status(msg)) {
            return _Z_RES_OK;
        }
#endif
        return _z_unicast_transport_peer_flush_buffer(&peer->_link, peer, tx_buf);
#if Z_FEATURE_BATCHING == 1
    } else if (!new_frame) {
        // reset the buffer to previous state, flush it and retry sending again on an empty one
        _z_wbuf_set_wpos(tx_buf, prev_wpos);
        _z_unicast_transport_peer_flush_buffer(&peer->_link, peer, tx_buf);
        return _z_unicast_transport_manager_send_n_msg_to_peer(manager, msg, reliability, peer_id);
#endif
    } else {
        _z_wbuf_reset(tx_buf);
    }
#if Z_FEATURE_FRAGMENTATION == 1
    ret = _z_unicast_transport_peer_send_fragment(&peer->_link, peer, tx_buf, msg, reliability);
#endif
    return ret;
}

z_result_t _z_unicast_transport_manager_send_n_msg(_z_unicast_transport_manager_t *manager,
                                                   const _z_network_message_t *msg, z_reliability_t reliability,
                                                   _z_destination_filter_t dest_filter) {
    z_result_t ret = _Z_ERR_TRANSPORT_NOT_AVAILABLE;  // to return if no peer is available in client mode
    if (_z_destination_filter_is_what(&dest_filter)) {
        z_what_t what = *_z_destination_filter_get_what(&dest_filter);
        for (_z_unicast_peer_slot_id_t iter = _z_unicast_transport_peer_established_begin(manager);
             iter != _z_unicast_transport_peer_hmap_end(&manager->_peers);
             iter = _z_unicast_transport_peer_established_iter_next(manager, iter)) {
            _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_at(&manager->_peers, iter);
            if (((z_what_t)peer->_remote_whatami & what) == 0) {
                continue;
            }
            ret = _z_unicast_transport_manager_send_n_msg_to_peer(manager, msg, reliability, (size_t)iter);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to send message to unicast peer %zu with err %d", (size_t)iter, ret);
            }
        }
    } else if (_z_destination_filter_is_mask(&dest_filter)) {
        _z_peer_mask_bitset_t *mask = _z_destination_filter_get_mask(&dest_filter);
        for (_z_peer_mask_bitset_iter_t iter = _z_peer_mask_bitset_begin_true(mask);
             iter != _z_peer_mask_bitset_end(mask) && iter < Z_MAX_NUM_UNICAST_PEERS;
             iter = _z_peer_mask_bitset_iter_next_true(mask, iter)) {
            ret = _z_unicast_transport_manager_send_n_msg_to_peer(manager, msg, reliability, (size_t)iter);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to send message to unicast peer %zu with err %d", (size_t)iter, ret);
            }
        }
    } else if (_z_destination_filter_is_single(&dest_filter)) {
        size_t peer_id = (size_t)*_z_destination_filter_get_single(&dest_filter);
        if (peer_id < Z_MAX_NUM_UNICAST_PEERS) {
            ret = _z_unicast_transport_manager_send_n_msg_to_peer(manager, msg, reliability, peer_id);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to send message to unicast peer %zu with err %d", peer_id, ret);
            }
        }
    }
    return manager->_parent->_session->_mode == Z_WHATAMI_CLIENT ? ret : _Z_RES_OK;
}

z_result_t _z_unicast_transport_manager_send_t_msg_to_peer(_z_unicast_transport_manager_t *manager,
                                                           const _z_transport_message_t *t_msg, size_t peer_id) {
    _z_unicast_transport_peer_t *peer = _z_unicast_transport_manager_get_established_peer(manager, peer_id);
    if (peer == NULL) {
        return _Z_ERR_INVALID;
    }
    _Z_DEBUG("Send transport message on transport unicast");
#if Z_FEATURE_BATCHING == 0
    _z_wbuf_t *tx_buf = &manager->_parent->_tx_buffer;
    // FIXME: temporary reduce the size of the buffer to that of peer MTU, if it is smaller than the default buffer size
#else
    _z_wbuf_t *tx_buf = &peer->_tx_buffer;
    if (_z_wbuf_len(tx_buf) > 0) {
        _Z_RETURN_IF_ERR(_z_unicast_transport_peer_flush_buffer(&peer->_link, peer, tx_buf));
    }

#endif
    _z_wbuf_prepare(tx_buf, _z_unicast_link_is_streamed(&peer->_link));
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_message_encode(tx_buf, t_msg), _z_wbuf_reset(tx_buf));
    return _z_unicast_transport_peer_flush_buffer(&peer->_link, peer, tx_buf);
}

#if Z_FEATURE_BATCHING == 1
z_result_t _z_unicast_transport_manager_send_n_batch(_z_unicast_transport_manager_t *manager) {
    z_result_t res = _Z_RES_OK;
    for (_z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_established_begin(manager);
         id != _z_unicast_transport_peer_hmap_end(&manager->_peers);
         id = _z_unicast_transport_peer_established_iter_next(manager, id)) {
        _z_unicast_transport_peer_t *peer = _z_unicast_transport_peer_at(&manager->_peers, id);
        if (_z_wbuf_len(&peer->_tx_buffer) > 0) {
            z_result_t ret = _z_unicast_transport_peer_flush_buffer(&peer->_link, peer, &peer->_tx_buffer);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to flush peer %zu buffer with err %d", (size_t)id, ret);
                res = ret;
            }
        }
    }
    return res;
}
#endif

#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
