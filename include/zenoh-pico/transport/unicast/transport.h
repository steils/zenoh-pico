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

#ifndef ZENOH_PICO_UNICAST_TRANSPORT_H
#define ZENOH_PICO_UNICAST_TRANSPORT_H

#include <stddef.h>
#include <string.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/transport/unicast/listener.h"
#include "zenoh-pico/transport/unicast/peer.h"
#include "zenoh-pico/utils/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_UNICAST_TRANSPORT == 1

#if Z_FEATURE_UNICAST_PEER == 1
#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_TYPE _z_unicast_transport_listener_t
#define _ZP_STATIC_VECTOR_TEMPLATE_NAME _z_unicast_transport_listener_vec
#define _ZP_STATIC_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_UNICAST_LISTENERS
#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_DESTROY_FN _z_unicast_transport_listener_clear

// default move
#include "zenoh-pico/collections/static_vector_template.h"
#endif

typedef struct _z_transport_manager_t _z_transport_manager_t;

typedef uint8_t _z_unicast_peer_slot_state_t;
enum {
    _Z_UNICAST_SLOT_FREE,
    _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK,
    _Z_UNICAST_HS_OPEN_WAIT_OPEN_ACK,
    _Z_UNICAST_HS_ACCEPT_WAIT_INIT,
    _Z_UNICAST_HS_ACCEPT_WAIT_OPEN,
    _Z_UNICAST_SLOT_ESTABLISHED,
};

typedef enum {
    _Z_UNICAST_HANDSHAKE_ROLE_OPEN,
    _Z_UNICAST_HANDSHAKE_ROLE_ACCEPT,
} _z_unicast_handshake_role_t;

// Tagged immutable peer identity. Numeric keys identify links that do not need
// address lookup; a later UDP P2P key can store a canonical source endpoint.
#define _Z_UNICAST_PEER_KEY_DATA_CAPACITY 22
#define _Z_UNICAST_PEER_KEY_KIND_NUMERIC 0
#define _Z_UNICAST_PEER_KEY_KIND_ADDRESS 1

typedef struct {
    uint8_t _kind;
    uint8_t _len;
    uint8_t _data[_Z_UNICAST_PEER_KEY_DATA_CAPACITY];
} _z_unicast_peer_key_t;

static inline _z_unicast_peer_key_t _z_unicast_peer_key_numeric(uint64_t id) {
    _z_unicast_peer_key_t key = {0};
    key._kind = _Z_UNICAST_PEER_KEY_KIND_NUMERIC;
    key._len = sizeof(id);
    for (size_t i = 0; i < sizeof(id); i++) {
        key._data[i] = (uint8_t)(id >> (i * 8));
    }
    return key;
}

static inline bool _z_unicast_peer_key_eq(const _z_unicast_peer_key_t *left, const _z_unicast_peer_key_t *right) {
    return left->_kind == right->_kind && left->_len == right->_len &&
           memcmp(left->_data, right->_data, left->_len) == 0;
}

static inline size_t _z_unicast_peer_key_hash(const _z_unicast_peer_key_t *key) {
    return _z_fnv1_hash((const uint8_t *)key, key->_len + offsetof(_z_unicast_peer_key_t, _data));
}

#define _ZP_STATIC_HASHMAP_TEMPLATE_KEY_TYPE _z_unicast_peer_key_t
#define _ZP_STATIC_HASHMAP_TEMPLATE_VAL_TYPE _z_unicast_transport_peer_t
#define _ZP_STATIC_HASHMAP_TEMPLATE_KEY_HASH_FN _z_unicast_peer_key_hash
#define _ZP_STATIC_HASHMAP_TEMPLATE_KEY_EQ_FN _z_unicast_peer_key_eq
#define _ZP_STATIC_HASHMAP_TEMPLATE_NAME _z_unicast_transport_peer_hmap
#define _ZP_STATIC_HASHMAP_TEMPLATE_CAPACITY Z_MAX_NUM_UNICAST_PEERS
#define _ZP_STATIC_HASHMAP_TEMPLATE_VAL_DESTROY_FN _z_unicast_transport_peer_clear
#include "zenoh-pico/collections/static_hashmap_template.h"

typedef _z_unicast_transport_peer_hmap_iter_t _z_unicast_peer_slot_id_t;

static inline _z_unicast_transport_peer_t *_z_unicast_transport_peer_at(_z_unicast_transport_peer_hmap_t *peers,
                                                                        _z_unicast_peer_slot_id_t id) {
    return &_z_unicast_transport_peer_hmap_at(peers, id)->val;
}

static inline const _z_unicast_transport_peer_t *_z_unicast_transport_peer_const_at(
    const _z_unicast_transport_peer_hmap_t *peers, _z_unicast_peer_slot_id_t id) {
    return &_z_unicast_transport_peer_hmap_const_at(peers, id)->val;
}

static inline bool _z_unicast_peer_slot_state_is_pending(_z_unicast_peer_slot_state_t state) {
    return state != _Z_UNICAST_SLOT_FREE && state != _Z_UNICAST_SLOT_ESTABLISHED;
}

static inline bool _z_unicast_handshake_state_is_open(_z_unicast_peer_slot_state_t state) {
    return state == _Z_UNICAST_HS_OPEN_WAIT_INIT_ACK || state == _Z_UNICAST_HS_OPEN_WAIT_OPEN_ACK;
}

static inline int _z_unicast_transport_peer_lease_cmp(const _z_unicast_peer_slot_id_t *a,
                                                      const _z_unicast_peer_slot_id_t *b,
                                                      const _z_unicast_transport_peer_hmap_t *ctx) {
    const _z_unicast_transport_peer_t *pa = &_z_unicast_transport_peer_hmap_const_at(ctx, *a)->val;
    const _z_unicast_transport_peer_t *pb = &_z_unicast_transport_peer_hmap_const_at(ctx, *b)->val;
    return zp_clock_compare(&pa->_lease_deadline, &pb->_lease_deadline);
}

#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE _z_unicast_peer_slot_id_t
#define _ZP_STATIC_PQUEUE_TEMPLATE_NAME _z_unicast_lease_pqueue
#define _ZP_STATIC_PQUEUE_TEMPLATE_SIZE Z_MAX_NUM_UNICAST_PEERS
#define _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE const _z_unicast_transport_peer_hmap_t
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN _z_unicast_transport_peer_lease_cmp
#include "zenoh-pico/collections/static_pqueue_template.h"

#if Z_FEATURE_UNICAST_PEER == 1
typedef struct _z_listen_data_t {
    size_t current_peer;
} _z_listen_data_t;

static inline void _z_listen_data_init(_z_listen_data_t *data) { data->current_peer = 0; }
#endif
typedef struct _z_unicast_transport_manager_t {
#if Z_FEATURE_UNICAST_PEER == 1
    _z_unicast_transport_listener_vec_t _listeners;
#endif
    _z_transport_manager_t *_parent;  // non-owning pointer to the global transport manager
    _z_unicast_transport_peer_hmap_t _peers;
    uint64_t _next_numeric_peer_id;
    // Keep lifecycle state outside peer records to avoid changing their layout
    _z_unicast_peer_slot_state_t _peer_states[Z_MAX_NUM_UNICAST_PEERS];
    _z_unicast_lease_pqueue_t _lease_pqueue;
    _z_zbuf_t _rx_buffer;  // a common buffer used for incoming messages on datagram links, to be shared among all peers
#if Z_FEATURE_UNICAST_PEER == 1
    _z_fut_handle_t _listen_task;
    _z_listen_data_t _listen_data;
#endif
} _z_unicast_transport_manager_t;

static inline _z_unicast_peer_slot_id_t _z_unicast_transport_peer_established_iter_next(
    const _z_unicast_transport_manager_t *manager, _z_unicast_peer_slot_id_t id) {
    const _z_unicast_transport_peer_hmap_t *peers = &manager->_peers;
    for (id = _z_unicast_transport_peer_hmap_iter_next(peers, id); id != _z_unicast_transport_peer_hmap_end(peers);
         id = _z_unicast_transport_peer_hmap_iter_next(peers, id)) {
        if (manager->_peer_states[id] == _Z_UNICAST_SLOT_ESTABLISHED) {
            return id;
        }
    }
    return _z_unicast_transport_peer_hmap_end(peers);
}

static inline _z_unicast_peer_slot_id_t _z_unicast_transport_peer_established_begin(
    const _z_unicast_transport_manager_t *manager) {
    _z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_hmap_begin(&manager->_peers);
    if (id != _z_unicast_transport_peer_hmap_end(&manager->_peers) &&
        manager->_peer_states[id] != _Z_UNICAST_SLOT_ESTABLISHED) {
        id = _z_unicast_transport_peer_established_iter_next(manager, id);
    }
    return id;
}

static inline size_t _z_unicast_transport_peer_established_count(const _z_unicast_transport_manager_t *manager) {
    size_t count = 0;
    for (_z_unicast_peer_slot_id_t id = _z_unicast_transport_peer_established_begin(manager);
         id != _z_unicast_transport_peer_hmap_end(&manager->_peers);
         id = _z_unicast_transport_peer_established_iter_next(manager, id)) {
        count++;
    }
    return count;
}

void _z_unicast_handshake_start_open(_z_unicast_transport_peer_t *peer, _z_unicast_peer_slot_state_t *state,
                                     uint16_t batch_size, const _z_id_t *local_zid, z_whatami_t mode,
                                     _z_transport_message_t *output);
void _z_unicast_handshake_start_accept(_z_unicast_transport_peer_t *peer, _z_unicast_peer_slot_state_t *state,
                                       z_whatami_t mode);
z_result_t _z_unicast_handshake_on_input(_z_unicast_transport_peer_t *peer, _z_unicast_peer_slot_state_t *state,
                                         const _z_id_t *local_zid, z_whatami_t local_whatami,
                                         const _z_transport_message_t *input, _z_transport_message_t *output,
                                         bool *has_output, bool *complete);

typedef z_result_t (*_z_unicast_handshake_send_f)(void *context, const _z_transport_message_t *message);
typedef z_result_t (*_z_unicast_handshake_recv_f)(void *context, _z_transport_message_t *message, _z_zbuf_t *zbuf,
                                                  z_clock_t deadline);

z_result_t _z_unicast_handshake_drive_sync(_z_unicast_transport_peer_t *peer, void *io_context,
                                           _z_unicast_handshake_send_f send_f, _z_unicast_handshake_recv_f recv_f,
                                           uint16_t batch_size, const _z_id_t *local_zid, z_whatami_t mode,
                                           _z_unicast_handshake_role_t role, _z_zbuf_t *opt_rx_leftover);

z_result_t _z_unicast_transport_manager_create(_z_unicast_transport_manager_t *manager, _z_transport_manager_t *parent);
z_result_t _z_unicast_transport_manager_spawn_tasks(_z_unicast_transport_manager_t *manager);

// Moves *link into the manager on success; ownership remains with the caller on failure.
// rx_leftover (optional, may be NULL) carries bytes read together with the handshake response that must seed the
// peer rx buffer; add_peer copies them into the peer rx buffer and leaves ownership of rx_leftover to the caller.
z_result_t _z_unicast_transport_manager_add_peer(_z_unicast_transport_manager_t *manager,
                                                 _z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                                 _z_connect_peer_id_t locator_id, _z_zbuf_t *opt_rx_leftover);

void _z_unicast_transport_manager_close(_z_unicast_transport_manager_t *manager);
void _z_unicast_transport_manager_clear(_z_unicast_transport_manager_t *manager);
z_result_t _z_unicast_transport_manager_close_peer(_z_unicast_transport_manager_t *manager,
                                                   _z_unicast_peer_slot_id_t peer_id,
                                                   const _z_close_reason_t *opt_reason,
                                                   _z_unicast_peer_slot_id_t *opt_next_peer_id);

z_result_t _z_unicast_handshake_listen(_z_unicast_transport_peer_t *peer, _z_unicast_link_t *link,
                                       const _z_id_t *local_zid, z_whatami_t mode);

// Moves *link into the manager on success; ownership remains with the caller on failure
z_result_t _z_unicast_transport_manager_accept_peer(_z_unicast_transport_manager_t *manager, _z_unicast_link_t *link);
bool _z_unicast_transport_manager_has_pending_locator(const _z_unicast_transport_manager_t *manager,
                                                      _z_connect_peer_id_t locator_id);
bool _z_unicast_transport_manager_has_pending_open(const _z_unicast_transport_manager_t *manager);
size_t _z_unicast_transport_manager_get_pending_count(const _z_unicast_transport_manager_t *manager);
void _z_unicast_transport_manager_abort_pending(_z_unicast_transport_manager_t *manager, _z_unicast_peer_slot_id_t id,
                                                z_result_t reason);
z_result_t _z_unicast_transport_manager_establish_pending(_z_unicast_transport_manager_t *manager,
                                                          _z_unicast_peer_slot_id_t id);

// Moves *link into the manager on success; ownership remains with the caller on failure
z_result_t _z_unicast_transport_manager_connect_peer(_z_unicast_transport_manager_t *manager, _z_unicast_link_t *link,
                                                     _z_connect_peer_id_t locator_id);
// Moves *link into the manager on success; ownership remains with the caller on failure
z_result_t _z_unicast_transport_manager_connect_peer_async(_z_unicast_transport_manager_t *manager,
                                                           _z_unicast_link_t *link, _z_connect_peer_id_t locator_id);
#if Z_FEATURE_UNICAST_PEER == 1
// listener is not consumed upon failure
z_result_t _z_unicast_transport_manager_add_listener(_z_unicast_transport_manager_t *manager,
                                                     _z_unicast_listener_t *listener,
                                                     _z_listen_listener_id_t locator_id);
#endif

static inline size_t _z_unicast_transport_manager_get_peers_count(const _z_unicast_transport_manager_t *manager) {
    return _z_unicast_transport_peer_established_count(manager);
}

static inline size_t _z_unicast_transport_manager_get_readers_count(const _z_unicast_transport_manager_t *manager) {
    return _z_unicast_transport_peer_hmap_size(&manager->_peers);
}

// Returns the next pending-handshake or peer lease deadline, or NULL if the queue is empty
const z_clock_t *_z_unicast_transport_manager_check_lease(_z_unicast_transport_manager_t *manager);

#if Z_FEATURE_UNICAST_PEER == 1
_z_fut_fn_result_t _zp_unicast_transport_listen_task_fn(void *unicast_transport_manager, _z_executor_t *executor);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_UNICAST_TRANSPORT_H */
