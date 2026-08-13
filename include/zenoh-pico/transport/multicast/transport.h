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

#ifndef ZENOH_PICO_MULTICAST_TRANSPORT_H
#define ZENOH_PICO_MULTICAST_TRANSPORT_H

#include "zenoh-pico/collections/slice.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/transport/multicast/group.h"
#include "zenoh-pico/transport/multicast/peer.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

#define _ZP_STATIC_HASHMAP_TEMPLATE_KEY_TYPE _z_link_address_t
#define _ZP_STATIC_HASHMAP_TEMPLATE_VAL_TYPE _z_multicast_transport_peer_t
#define _ZP_STATIC_HASHMAP_TEMPLATE_KEY_HASH_FN _z_link_address_hash
#define _ZP_STATIC_HASHMAP_TEMPLATE_KEY_EQ_FN _z_link_address_eq
#define _ZP_STATIC_HASHMAP_TEMPLATE_NAME _z_address_to_multicast_transport_peer_hmap
#define _ZP_STATIC_HASHMAP_TEMPLATE_CAPACITY Z_MAX_NUM_MULTICAST_PEERS
#define _ZP_STATIC_HASHMAP_TEMPLATE_VAL_DESTROY_FN _z_multicast_transport_peer_clear
// default move
#include "zenoh-pico/collections/static_hashmap_template.h"

#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_TYPE _z_multicast_transport_group_t
#define _ZP_STATIC_VECTOR_TEMPLATE_NAME _z_multicast_transport_group_vec
#define _ZP_STATIC_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_MULTICAST_LISTENERS
#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_DESTROY_FN _z_multicast_transport_group_clear
// default move
#include "zenoh-pico/collections/static_vector_template.h"

#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_NAME _z_mcast_group_mask_bitset
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_MULTICAST_LISTENERS
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_BLOCK_TYPE uint8_t
#define _ZP_STATIC_BIT_VECTOR_TEMPLATE_IS_SET 1
#include "zenoh-pico/collections/static_bit_vector_template.h"

static inline int _z_multicast_transport_peer_lease_cmp(const _z_address_to_multicast_transport_peer_hmap_iter_t *a,
                                                        const _z_address_to_multicast_transport_peer_hmap_iter_t *b,
                                                        const _z_address_to_multicast_transport_peer_hmap_t *ctx) {
    return zp_clock_compare(&_z_address_to_multicast_transport_peer_hmap_const_at(ctx, *a)->val._lease_deadline,
                            &_z_address_to_multicast_transport_peer_hmap_const_at(ctx, *b)->val._lease_deadline);
}

#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE _z_address_to_multicast_transport_peer_hmap_iter_t
#define _ZP_STATIC_PQUEUE_TEMPLATE_NAME _z_multicast_lease_pqueue
#define _ZP_STATIC_PQUEUE_TEMPLATE_SIZE Z_MAX_NUM_MULTICAST_PEERS
#define _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE const _z_address_to_multicast_transport_peer_hmap_t
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN _z_multicast_transport_peer_lease_cmp
#include "zenoh-pico/collections/static_pqueue_template.h"

typedef struct _z_transport_manager_t _z_transport_manager_t;
typedef struct _z_multicast_transport_manager_t {
    _z_multicast_transport_group_vec_t _groups;
    _z_transport_manager_t *_parent;  // non-owning pointer to the global transport manager
    _z_address_to_multicast_transport_peer_hmap_t _peers;
    _z_fut_handle_t _send_join_task_handle;
    _z_multicast_lease_pqueue_t _lease_pqueue;
    _z_zint_t _sn_res;  // shared between all multicast peers
} _z_multicast_transport_manager_t;

z_result_t _z_multicast_transport_manager_create(_z_multicast_transport_manager_t *manager,
                                                 _z_transport_manager_t *parent);
z_result_t _z_multicast_transport_manager_spawn_tasks(_z_multicast_transport_manager_t *manager);

// link is not consumed upon failure
z_result_t _z_multicast_transport_manager_add_group(_z_multicast_transport_manager_t *manager,
                                                    _z_multicast_link_t *link, _z_listen_listener_id_t locator_id);

void _z_multicast_transport_manager_close(_z_multicast_transport_manager_t *manager);
void _z_multicast_transport_manager_clear(_z_multicast_transport_manager_t *manager);
z_result_t _z_multicast_transport_manager_close_peer(
    _z_multicast_transport_manager_t *manager, _z_address_to_multicast_transport_peer_hmap_iter_t peer_id,
    const _z_close_reason_t *opt_reason, _z_address_to_multicast_transport_peer_hmap_iter_t *opt_next_peer_id);

_z_fut_fn_result_t _z_multicast_transport_manager_send_join_task_fn(void *multicast_transport_manager,
                                                                    _z_executor_t *executor);

static inline size_t _z_multicast_transport_manager_get_peers_count(const _z_multicast_transport_manager_t *manager) {
    return _z_address_to_multicast_transport_peer_hmap_size(&manager->_peers);
}

static inline size_t _z_multicast_transport_manager_get_readers_count(const _z_multicast_transport_manager_t *manager) {
    return _z_multicast_transport_group_vec_size(&manager->_groups);
}

// Returns the next lease deadline if there are any remaining peers, or NULL if there are no peers.
const z_clock_t *_z_multicast_transport_manager_check_lease(_z_multicast_transport_manager_t *manager);
#endif

#ifdef __cplusplus
}
#endif
#endif /* ZENOH_PICO_MULTICAST_TRANSPORT_H */
