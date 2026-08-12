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
//

#include "zenoh-pico/transport/tx.h"

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/protocol/codec/core.h"
#include "zenoh-pico/protocol/codec/network.h"
#include "zenoh-pico/protocol/codec/transport.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/transport/multicast/tx.h"
#include "zenoh-pico/transport/raweth/tx.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/unicast/tx.h"
#include "zenoh-pico/transport/utils.h"
#include "zenoh-pico/utils/endianness.h"
#include "zenoh-pico/utils/logging.h"

void _z_wbuf_prepare(_z_wbuf_t *buf, bool is_streamed) {
    if (is_streamed) {
        for (uint8_t i = 0; i < _Z_MSG_LEN_ENC_SIZE; i++) {
            _z_wbuf_put(buf, 0, i);
        }
        _z_wbuf_set_wpos(buf, _Z_MSG_LEN_ENC_SIZE);
    }
}

void _z_wbuf_finalize(_z_wbuf_t *buf, bool is_streamed) {
    if (is_streamed) {
        size_t len = _z_wbuf_len(buf) - _Z_MSG_LEN_ENC_SIZE;
        // Encode the u16 size as little endian
        _z_wbuf_put(buf, _z_get_u16_lsb((uint_fast16_t)len), 0);
        _z_wbuf_put(buf, _z_get_u16_msb((uint_fast16_t)len), 1);
    }
}

z_result_t _z_wbuf_serialize_zenoh_fragment(_z_wbuf_t *dst, _z_wbuf_t *src, z_reliability_t reliability, size_t sn,
                                            bool first) {
    z_result_t ret = _Z_RES_OK;

    // Assume first that this is not the final fragment
    bool is_final = false;
    do {
        size_t w_pos = _z_wbuf_get_wpos(dst);  // Mark the buffer for the writing operation

        _z_transport_message_t f_hdr =
            _z_t_msg_make_fragment_header(sn, reliability == Z_RELIABILITY_RELIABLE, is_final, first, false);
        ret = _z_transport_message_encode(dst, &f_hdr);  // Encode the frame header
        if (ret == _Z_RES_OK) {
            size_t space_left = _z_wbuf_space_left(dst);
            size_t bytes_left = _z_wbuf_len(src);

            if ((is_final == false) && (bytes_left <= space_left)) {  // Check if it is really the final fragment
                _z_wbuf_set_wpos(dst, w_pos);                         // Revert the buffer
                is_final = true;  // It is really the finally fragment, reserialize the header
                continue;
            }

            size_t to_copy = (bytes_left <= space_left) ? bytes_left : space_left;  // Compute bytes to write
            ret = _z_wbuf_siphon(dst, src, to_copy);                                // Write the fragment
        }
        break;
    } while (1);

    return ret;
}

z_result_t _z_transport_manager_send_n_msg(_z_transport_manager_t *manager, const _z_network_message_t *z_msg,
                                           z_reliability_t reliability, _z_destination_filter_t dest_filter,
                                           _z_transport_type_t transport_type) {
    z_result_t ret = _Z_RES_OK;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    if ((transport_type & _Z_TRANSPORT_TYPE_UNICAST) != 0) {
        ret = _z_unicast_transport_manager_send_n_msg(&manager->_unicast, z_msg, reliability, dest_filter);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Failed to send message over unicast with err %d", ret);
        }
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    if ((transport_type & _Z_TRANSPORT_TYPE_MULTICAST) != 0) {
        ret = _z_multicast_transport_manager_send_n_msg(&manager->_multicast, z_msg, reliability, dest_filter);
        if (ret != _Z_RES_OK) {
            _Z_ERROR("Failed to send message over multicast with err %d", ret);
        }
    }
#endif
    return manager->_session->_mode == Z_WHATAMI_CLIENT ? ret : _Z_RES_OK;
}

#if Z_FEATURE_BATCHING == 1
z_result_t _z_transport_manager_send_n_batch(_z_transport_manager_t *manager) {
    z_result_t ret = _Z_RES_OK;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    ret = _z_unicast_transport_manager_send_n_batch(&manager->_unicast);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("Failed to send batch over unicast with err %d", ret);
    }
#endif
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    ret = _z_multicast_transport_manager_send_n_batch(&manager->_multicast);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("Failed to send batch over multicast with err %d", ret);
    }
#endif
    return manager->_session->_mode == Z_WHATAMI_CLIENT ? ret : _Z_RES_OK;
}

z_result_t _z_transport_manager_stop_batching(_z_transport_manager_t *manager) {
    z_result_t ret = _Z_RES_OK;
    if (manager->_batching_enabled) {
        ret = _z_transport_manager_send_n_batch(manager);
        manager->_batching_enabled = false;
    }
    return ret;
}

z_result_t _z_transport_manager_start_batching(_z_transport_manager_t *manager) {
    if (manager->_batching_enabled) {
        return Z_ALREADY;
    } else {
        manager->_batching_enabled = true;
        return _Z_RES_OK;
    }
}
#endif

z_result_t _z_transport_manager_send_keep_alive(_z_transport_manager_t *manager) {
    _z_transport_message_t msg = _z_t_msg_make_keep_alive();

    // Note: we need to lock transport manager to ensure no other thread is sending messages while we are sending the
    // keep alive message to all peers.
    z_result_t ret = _z_transport_manager_lock(manager);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("Failed to lock transport manager for sending keep alive %d", ret);
        return ret;
    }
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_address_to_unicast_transport_peer_hmap_elem_t *entry;
    _ZP_FOREACH_FILTERED (_z_address_to_unicast_transport_peer_hmap, &manager->_unicast._peers, entry,
                          entry->val._state == _Z_UNICAST_PEER_ESTABLISHED) {
        _z_unicast_transport_peer_t *peer = &entry->val;
        if (!peer->_transmitted) {
            _z_unicast_transport_manager_send_t_msg_to_peer(&manager->_unicast, &msg, (size_t)entry_iter);
            // TODO: handle the error
        }
        peer->_transmitted = false;
    }
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_multicast_transport_group_t *group = NULL;
    _ZP_FOREACH (_z_multicast_transport_group_vec, &manager->_multicast._groups, group) {
        if (!group->_transmitted) {
            _z_multicast_transport_manager_send_t_msg_to_group(&manager->_multicast, &msg, group_iter);
            // TODO: handle the error
        }
        group->_transmitted = false;
    }
#endif
    _z_transport_manager_unlock(manager);
    return _Z_RES_OK;
}
