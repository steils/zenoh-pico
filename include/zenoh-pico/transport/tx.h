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

#ifndef ZENOH_PICO_TRANSPORT_TX_H
#define ZENOH_PICO_TRANSPORT_TX_H

#include "zenoh-pico/net/session.h"
#include "zenoh-pico/transport/destination_filter.h"
#include "zenoh-pico/transport/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

void _z_wbuf_prepare(_z_wbuf_t *buf, bool is_streamed);
void _z_wbuf_finalize(_z_wbuf_t *buf, bool is_streamed);
z_result_t _z_wbuf_serialize_zenoh_fragment(_z_wbuf_t *dst, _z_wbuf_t *src, z_reliability_t reliability, size_t sn,
                                            bool first);

// Transport manager should be locked prior to calling this function outside of background thread.
z_result_t _z_transport_manager_send_n_msg(_z_transport_manager_t *manager, const _z_network_message_t *n_msg,
                                           z_reliability_t reliability, _z_destination_filter_t dest_filter,
                                           _z_transport_type_t transport_type);

// Temporary helper function to send a network message with locking.
// Normally lock should be acquired outside of this function, when constucting _z_destination_filter_t
static inline z_result_t _z_transport_manager_send_n_msg_with_lock(
    _z_transport_manager_t *manager, const _z_network_message_t *n_msg, z_reliability_t reliability,
    z_congestion_control_t cong_ctrl, _z_destination_filter_t dest_filter, _z_transport_type_t transport_type) {
    if (cong_ctrl == Z_CONGESTION_CONTROL_BLOCK) {
        _Z_RETURN_IF_ERR(_z_transport_manager_lock(manager));
    } else {
        _Z_RETURN_IF_ERR(_z_transport_manager_try_lock(manager));
    }
    z_result_t ret = _z_transport_manager_send_n_msg(manager, n_msg, reliability, dest_filter, transport_type);
    _z_transport_manager_unlock(manager);
    return ret;
}

#if Z_FEATURE_BATCHING == 1
// Transport manager should be locked prior to calling this function outside of background thread.
z_result_t _z_transport_manager_send_n_batch(_z_transport_manager_t *manager);
// Transport manager should be locked prior to calling this function outside of background thread.
z_result_t _z_transport_manager_stop_batching(_z_transport_manager_t *manager);
// Transport manager should be locked prior to calling this function outside of background thread.
z_result_t _z_transport_manager_start_batching(_z_transport_manager_t *manager);
#endif

z_result_t _z_transport_manager_send_keep_alive(_z_transport_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_TRANSPORT_TX_H */
