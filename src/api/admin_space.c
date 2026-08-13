//
// Copyright (c) 2025 ZettaScale Technology
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

#include "zenoh-pico/api/admin_space.h"

#include "zenoh-pico/api/encoding.h"
#include "zenoh-pico/api/primitives.h"
#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/net/primitives.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/transport/admin_space.h"
#include "zenoh-pico/transport/connectivity.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/utils/hash.h"
#include "zenoh-pico/utils/json_encoder.h"
#include "zenoh-pico/utils/uuid.h"

#if Z_FEATURE_ADMIN_SPACE == 1

#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_TYPE char
#define _ZP_STATIC_VECTOR_TEMPLATE_NAME _z_admin_space_ke_buf
#define _ZP_STATIC_VECTOR_TEMPLATE_SIZE 128
#include "zenoh-pico/collections/static_vector_template.h"

z_result_t _z_json_encoder_write_zid(_z_json_encoder_t *je, const _z_id_t *value) {
    char buf[ZENOH_ID_SIZE * 2];
    size_t len = _ZP_ARRAY_SIZE(buf);
    _z_id_to_str(value, buf, &len);
    _z_string_view_t str_view = _z_string_view_make(buf, len);
    return _z_json_encoder_write_z_string(je, _z_string_view_deref(&str_view));
}

static void _ze_admin_space_write_ke_prefix(_z_admin_space_ke_buf_t *ke_buf, const z_id_t *zid) {
    // Every _ke builder starts here, so (re)initialize the buffer to an empty state. init() is safe
    // on uninitialized storage (it just memsets) and, because the element type is trivially
    // destructible, it doubles as a reset that supports reusing the same buffer for several ke.
    _z_admin_space_ke_buf_init(ke_buf);
    char zid_buf[ZENOH_ID_SIZE * 2];
    size_t zid_len = ZENOH_ID_SIZE * 2;
    _z_id_to_str(zid, zid_buf, &zid_len);
    _z_admin_space_ke_buf_append(ke_buf, _Z_KEYEXPR_AT "/", _Z_KEYEXPR_AT_LEN + 1);
    _z_admin_space_ke_buf_append(ke_buf, zid_buf, zid_len);
}

// ke = _Z_KEYEXPR_AT / ZID / WHATAMI
static void _ze_admin_space_pico_queryable_ke(_z_admin_space_ke_buf_t *ke_buf, const z_id_t *zid, z_whatami_t whatami) {
    _ze_admin_space_write_ke_prefix(ke_buf, zid);
    z_view_string_t whatami_str;
    z_whatami_to_view_string(whatami, &whatami_str);
    _z_admin_space_ke_buf_append(ke_buf, "/", 1);
    _z_admin_space_ke_buf_append(ke_buf, z_string_data(z_view_string_loan(&whatami_str)),
                                 z_string_len(z_view_string_loan(&whatami_str)));
}

#if Z_FEATURE_CONNECTIVITY == 1
// ke = _Z_KEYEXPR_AT / ZID / _Z_KEYEXPR_SESSION
static void _ze_admin_space_session_ke(_z_admin_space_ke_buf_t *ke_buf, const z_id_t *zid) {
    _ze_admin_space_write_ke_prefix(ke_buf, zid);
    _z_admin_space_ke_buf_append(ke_buf, "/" _Z_KEYEXPR_SESSION, 1 + _Z_KEYEXPR_SESSION_LEN);
}

// ke = _Z_KEYEXPR_AT / ZID / _Z_KEYEXPR_SESSION / Z_KEYEXPR_STARSTAR
static void _ze_admin_space_session_queryable_ke(_z_admin_space_ke_buf_t *ke_buf, const z_id_t *zid) {
    _ze_admin_space_session_ke(ke_buf, zid);
    _z_admin_space_ke_buf_append(ke_buf, "/" _Z_KEYEXPR_STARSTAR, 1 + _Z_KEYEXPR_STARSTAR_LEN);
}

// ke = _Z_KEYEXPR_AT / ZID / _Z_KEYEXPR_SESSION / _Z_KEYEXPR_TRANSPORT_UNICAST / PEER_ZID
static void _ze_admin_space_session_transport_ke(_z_admin_space_ke_buf_t *ke_buf, const z_id_t *zid,
                                                 const z_id_t *remote_zid, bool is_multicast) {
    _ze_admin_space_session_ke(ke_buf, zid);
    if (is_multicast) {
        _z_admin_space_ke_buf_append(ke_buf, "/" _Z_KEYEXPR_TRANSPORT_MULTICAST,
                                     1 + _Z_KEYEXPR_TRANSPORT_MULTICAST_LEN);
    } else {
        _z_admin_space_ke_buf_append(ke_buf, "/" _Z_KEYEXPR_TRANSPORT_UNICAST, 1 + _Z_KEYEXPR_TRANSPORT_UNICAST_LEN);
    }
    _z_admin_space_ke_buf_push_back(ke_buf, "/");
    char zid_buf[ZENOH_ID_SIZE * 2];
    size_t zid_len = ZENOH_ID_SIZE * 2;
    _z_id_to_str(remote_zid, zid_buf, &zid_len);
    _z_admin_space_ke_buf_append(ke_buf, zid_buf, zid_len);
}

static size_t _z_link_info_hash(const _z_info_link_t *link) {
    size_t hash = _z_fnv1_hash((uint8_t *)_z_string_data(&link->_src), z_string_len(&link->_src));
    size_t hash_current = _z_fnv1_hash((uint8_t *)_z_string_data(&link->_dst), z_string_len(&link->_dst));
    hash = _z_hash_combine(hash, hash_current);
    if (!_z_string_is_empty(&link->_mcast_group)) {
        hash_current = _z_fnv1_hash((uint8_t *)_z_string_data(&link->_mcast_group), z_string_len(&link->_mcast_group));
        hash = _z_hash_combine(hash, hash_current);
    }
    hash = _z_hash_combine(hash, (size_t)link->_mtu);
    hash = _z_hash_combine(hash, (size_t)link->_is_streamed);
    hash = _z_hash_combine(hash, (size_t)link->_is_reliable);
    return hash;
}

// ke = _Z_KEYEXPR_AT / ZID / _Z_KEYEXPR_SESSION / _Z_KEYEXPR_TRANSPORT_UNICAST / PEER_ZID / _Z_KEYEXPR_LINK /
// LINK_HASH
static void _ze_admin_space_session_link_ke(_z_admin_space_ke_buf_t *ke_buf, const z_id_t *zid, const z_id_t *peer_zid,
                                            size_t link_hash, bool is_multicast) {
    _ze_admin_space_session_transport_ke(ke_buf, zid, peer_zid, is_multicast);
    _z_admin_space_ke_buf_append(ke_buf, "/" _Z_KEYEXPR_LINK, 1 + _Z_KEYEXPR_LINK_LEN);
    _z_admin_space_ke_buf_push_back(ke_buf, "/");
    char buf[21];  // Enough to hold the maximum size_t value in decimal
    int len = snprintf(buf, sizeof(buf), "%zu", link_hash);
    _z_admin_space_ke_buf_append(ke_buf, buf, (size_t)len);
}
#endif  // Z_FEATURE_CONNECTIVITY == 1

z_result_t _z_json_encoder_write_whatami(_z_json_encoder_t *je, z_whatami_t mode) {
    switch (mode) {
        case Z_WHATAMI_ROUTER:
            return _z_json_encoder_write_string(je, "router");
        case Z_WHATAMI_PEER:
            return _z_json_encoder_write_string(je, "peer");
        case Z_WHATAMI_CLIENT:
            return _z_json_encoder_write_string(je, "client");
        default:
            return _Z_ERR_INVALID;
    }
}

#if Z_FEATURE_CONNECTIVITY == 1
static z_result_t _ze_admin_space_encode_connectivity_transport_payload(z_owned_bytes_t *payload, const z_id_t *zid,
                                                                        z_whatami_t whatami, bool is_qos, bool is_shm) {
    z_internal_bytes_null(payload);

    _z_json_encoder_t je;
    _Z_RETURN_IF_ERR(_z_json_encoder_empty(&je));

    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_start_object(&je), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "zid"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_zid(&je, zid), _z_json_encoder_clear(&je));

    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "whatami"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_whatami(&je, whatami), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "is_qos"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_boolean(&je, is_qos), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "is_shm"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_boolean(&je, is_shm), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_end_object(&je), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_finish(&je, payload), _z_json_encoder_clear(&je));
    return _Z_RES_OK;
}

static z_result_t _ze_admin_space_encode_connectivity_link_payload(z_owned_bytes_t *payload,
                                                                   const _z_info_link_t *link) {
    z_internal_bytes_null(payload);

    _z_json_encoder_t je;
    _Z_RETURN_IF_ERR(_z_json_encoder_empty(&je));

    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_start_object(&je), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "src"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_z_string(&je, &link->_src), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "dst"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_z_string(&je, &link->_dst), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "group"), _z_json_encoder_clear(&je));
    if (_z_string_is_empty(&link->_mcast_group)) {
        _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_null(&je), _z_json_encoder_clear(&je));
    } else {
        _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_z_string(&je, &link->_mcast_group), _z_json_encoder_clear(&je));
    }
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "mtu"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_u64(&je, (uint64_t)link->_mtu), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "is_reliable"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_boolean(&je, link->_is_reliable), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_key(&je, "is_streamed"), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_write_boolean(&je, link->_is_streamed), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_end_object(&je), _z_json_encoder_clear(&je));
    _Z_CLEAN_RETURN_IF_ERR(_z_json_encoder_finish(&je, payload), _z_json_encoder_clear(&je));
    return _Z_RES_OK;
}
#endif  // Z_FEATURE_CONNECTIVITY == 1

static void _ze_admin_space_reply_null(_ze_admin_space_reply_t *reply) {
    z_internal_keyexpr_null(&reply->ke);
    z_internal_bytes_null(&reply->payload);
}

void _ze_admin_space_reply_clear(_ze_admin_space_reply_t *reply) {
    z_keyexpr_drop(z_keyexpr_move(&reply->ke));
    z_bytes_drop(z_bytes_move(&reply->payload));
    _ze_admin_space_reply_null(reply);
}

static z_result_t _ze_admin_space_encode_transports(_z_json_encoder_t *je, _z_session_t *session) {
    _Z_RETURN_IF_ERR(_z_transport_manager_lock(&session->_transport_manager));
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_manager_encode_transports_json(&session->_transport_manager, je),
                           _z_transport_manager_unlock(&session->_transport_manager));
    _z_transport_manager_unlock(&session->_transport_manager);
    return _Z_RES_OK;
}

static z_result_t _ze_admin_space_encode_locators(_z_json_encoder_t *je, _z_session_t *session) {
    _Z_RETURN_IF_ERR(_z_transport_manager_lock(&session->_transport_manager));
    _Z_CLEAN_RETURN_IF_ERR(_z_transport_manager_encode_locators_json(&session->_transport_manager, je),
                           _z_transport_manager_unlock(&session->_transport_manager));
    _z_transport_manager_unlock(&session->_transport_manager);
    return _Z_RES_OK;
}

static z_result_t _ze_admin_space_encode_session(_z_json_encoder_t *je, _z_session_t *session) {
    _Z_RETURN_IF_ERR(_z_json_encoder_start_object(je));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "zid"));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_zid(je, &session->_local_zid));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "version"));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_string(je, ZENOH_PICO_VERSION));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "metadata"));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_null(je));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "plugins"));
    _Z_RETURN_IF_ERR(_z_json_encoder_start_object(je));
    _Z_RETURN_IF_ERR(_z_json_encoder_end_object(je));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "locators"));
    _Z_RETURN_IF_ERR(_ze_admin_space_encode_locators(je, session));
    _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "sessions"));
    _Z_RETURN_IF_ERR(_ze_admin_space_encode_transports(je, session));
    _Z_RETURN_IF_ERR(_z_json_encoder_end_object(je));

    return _Z_RES_OK;
}

#if Z_FEATURE_CONNECTIVITY == 1
typedef struct _ze_admin_space_connectivity_query_arg_t {
    const z_loaned_query_t *query;
    const _z_session_t *session;
    _ze_admin_space_reply_list_t **replies;
    bool failed;
} _ze_admin_space_connectivity_query_arg_t;

static z_result_t _ze_admin_space_add_reply_bytes(const z_loaned_keyexpr_t *ke, z_moved_bytes_t *payload,
                                                  _ze_admin_space_reply_list_t **replies) {
    _ze_admin_space_reply_t *reply = z_malloc(sizeof(_ze_admin_space_reply_t));
    if (reply == NULL) {
        z_bytes_drop(payload);
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    _ze_admin_space_reply_null(reply);

    _Z_CLEAN_RETURN_IF_ERR(z_keyexpr_clone(&reply->ke, ke), z_bytes_drop(payload); z_free(reply));
    z_bytes_take(&reply->payload, payload);

    _ze_admin_space_reply_list_t *old = *replies;
    _ze_admin_space_reply_list_t *tmp = _ze_admin_space_reply_list_push(*replies, reply);
    if (tmp == old) {
        _ze_admin_space_reply_clear(reply);
        z_free(reply);
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    *replies = tmp;

    return _Z_RES_OK;
}

static void _ze_admin_space_add_transport_reply(z_loaned_transport_t *transport, void *arg) {
    _ze_admin_space_connectivity_query_arg_t *query_arg = (_ze_admin_space_connectivity_query_arg_t *)arg;
    if (query_arg->failed) {
        return;
    }

    z_view_keyexpr_t transport_ke;
    _z_admin_space_ke_buf_t ke_buf;
    _ze_admin_space_session_transport_ke(&ke_buf, &query_arg->session->_local_zid, &transport->_zid,
                                         transport->_is_multicast);
    z_view_keyexpr_from_substr_unchecked(&transport_ke, _z_admin_space_ke_buf_data(&ke_buf),
                                         _z_admin_space_ke_buf_size(&ke_buf));
    if (!z_keyexpr_intersects(z_query_keyexpr(query_arg->query), z_view_keyexpr_loan(&transport_ke))) {
        return;
    }
    z_owned_bytes_t payload;
    z_result_t ret = _ze_admin_space_encode_connectivity_transport_payload(&payload, &transport->_zid,
                                                                           transport->_whatami, false, false);
    _Z_SET_IF_OK(ret, _ze_admin_space_add_reply_bytes(z_view_keyexpr_loan(&transport_ke), z_bytes_move(&payload),
                                                      query_arg->replies));
    if (ret != _Z_RES_OK) {
        _Z_WARN("Failed to encode connectivity transport payload: %d", ret);
        query_arg->failed = true;
    }
}

static void _ze_admin_space_add_link_reply(z_loaned_link_t *link, void *arg) {
    _ze_admin_space_connectivity_query_arg_t *query_arg = (_ze_admin_space_connectivity_query_arg_t *)arg;
    if (query_arg->failed) {
        return;
    }

    z_view_keyexpr_t link_ke;
    _z_admin_space_ke_buf_t ke_buf;
    _ze_admin_space_session_link_ke(&ke_buf, &query_arg->session->_local_zid, &link->_zid, _z_link_info_hash(link),
                                    !_z_string_is_empty(&link->_mcast_group));
    z_view_keyexpr_from_substr_unchecked(&link_ke, _z_admin_space_ke_buf_data(&ke_buf),
                                         _z_admin_space_ke_buf_size(&ke_buf));
    if (!z_keyexpr_intersects(z_query_keyexpr(query_arg->query), z_view_keyexpr_loan(&link_ke))) {
        return;
    }
    z_owned_bytes_t payload;
    z_result_t ret = _ze_admin_space_encode_connectivity_link_payload(&payload, link);
    _Z_SET_IF_OK(ret, _ze_admin_space_add_reply_bytes(z_view_keyexpr_loan(&link_ke), z_bytes_move(&payload),
                                                      query_arg->replies));
    if (ret != _Z_RES_OK) {
        _Z_WARN("Failed to encode connectivity link payload: %d", ret);
        query_arg->failed = true;
    }
}

static void _ze_admin_space_query_handle_connectivity_session(const z_loaned_query_t *query, _z_session_t *session,
                                                              _ze_admin_space_reply_list_t **replies) {
    z_result_t ret = _z_transport_manager_lock(&session->_transport_manager);
    if (ret != _Z_RES_OK) {
        _Z_WARN("Failed to lock transport manager for connectivity session query: %d", ret);
        return;
    }
    _ze_admin_space_connectivity_query_arg_t query_arg = {
        .query = query,
        .session = session,
        .replies = replies,
        .failed = false,
    };
    _z_transport_manager_fetch_transports(&session->_transport_manager, _ze_admin_space_add_transport_reply,
                                          &query_arg);
    if (!query_arg.failed) {
        _z_transport_manager_fetch_links(&session->_transport_manager, _ze_admin_space_add_link_reply, &query_arg,
                                         NULL);
    }
    _z_transport_manager_unlock(&session->_transport_manager);
}

static void _ze_admin_space_query_reply_all(z_loaned_query_t *query, _ze_admin_space_reply_list_t **replies) {
    _ze_admin_space_reply_list_t *next = *replies;
    while (next != NULL) {
        _ze_admin_space_reply_t *reply = _ze_admin_space_reply_list_value(next);
        z_query_reply_options_t opt;
        z_query_reply_options_default(&opt);
        z_owned_encoding_t encoding;
        if (z_encoding_clone(&encoding, z_encoding_application_json()) == _Z_RES_OK) {
            opt.encoding = z_encoding_move(&encoding);
            z_result_t res = z_query_reply(query, z_keyexpr_loan(&reply->ke), z_bytes_move(&reply->payload), &opt);
            if (res != _Z_RES_OK) {
                z_view_string_t keystr;
                if (z_keyexpr_as_view_string(z_keyexpr_loan(&reply->ke), &keystr) == _Z_RES_OK) {
                    _Z_ERROR("Failed to reply to admin space query on key expression: %.*s",
                             (int)z_string_len(z_view_string_loan(&keystr)),
                             z_string_data(z_view_string_loan(&keystr)));
                } else {
                    _Z_ERROR("Failed to reply to admin space query");
                }
            }
        } else {
            _Z_ERROR("Failed to clone JSON encoding for admin space query reply");
            break;
        }
        next = _ze_admin_space_reply_list_next(next);
    }
}

#endif  // Z_FEATURE_CONNECTIVITY == 1

static void _ze_admin_space_pico_query_handler(z_loaned_query_t *query, void *ctx) {
    _z_session_weak_t *session_weak = (_z_session_weak_t *)ctx;

    _z_session_rc_t session_rc = _z_session_weak_upgrade_if_open(session_weak);
    if (_Z_RC_IS_NULL(&session_rc)) {
        _Z_ERROR("Dropped admin space query - session closed");
        return;
    }
    _z_session_t *session = _Z_RC_IN_VAL(&session_rc);

    z_owned_bytes_t payload;
    z_internal_bytes_null(&payload);

    _z_json_encoder_t je;
    _z_json_encoder_empty(&je);

    z_result_t ret = _ze_admin_space_encode_session(&je, session);
    if (ret == _Z_RES_OK) {
        ret = _z_json_encoder_finish(&je, &payload);
    }
    _z_json_encoder_clear(&je);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("Failed to encode session for admin space query: %d", ret);
    } else {
        z_query_reply_options_t opt;
        z_query_reply_options_default(&opt);
        z_owned_encoding_t encoding;
        if (z_encoding_clone(&encoding, z_encoding_application_json()) == _Z_RES_OK) {
            opt.encoding = z_encoding_move(&encoding);
            z_view_keyexpr_t ke;
            _z_admin_space_ke_buf_t ke_buf;
            _ze_admin_space_pico_queryable_ke(&ke_buf, &session->_local_zid, session->_mode);
            z_view_keyexpr_from_substr_unchecked(&ke, _z_admin_space_ke_buf_data(&ke_buf),
                                                 _z_admin_space_ke_buf_size(&ke_buf));
            ret = z_query_reply(query, z_view_keyexpr_loan(&ke), z_bytes_move(&payload), &opt);
            if (ret != _Z_RES_OK) {
                _Z_ERROR("Failed to reply to admin space query: %d", ret);
            }
        } else {
            z_bytes_drop(z_bytes_move(&payload));
            _Z_ERROR("Failed to clone JSON encoding for admin space query reply");
        }
    }

    _z_session_rc_drop(&session_rc);
}

#if Z_FEATURE_CONNECTIVITY == 1
static void _ze_admin_space_session_query_handler(z_loaned_query_t *query, void *ctx) {
    _z_session_weak_t *session_weak = (_z_session_weak_t *)ctx;

    _z_session_rc_t session_rc = _z_session_weak_upgrade_if_open(session_weak);
    if (_Z_RC_IS_NULL(&session_rc)) {
        _Z_ERROR("Dropped admin space query - session closed");
        return;
    }
    _z_session_t *session = _Z_RC_IN_VAL(&session_rc);
    _ze_admin_space_reply_list_t *replies = _ze_admin_space_reply_list_new();
    _ze_admin_space_query_handle_connectivity_session(query, session, &replies);

    _ze_admin_space_query_reply_all(query, &replies);
    _ze_admin_space_reply_list_free(&replies);
    _z_session_rc_drop(&session_rc);
}
#endif

static void _ze_admin_space_query_dropper(void *ctx) {
    _z_session_weak_t *session_weak = (_z_session_weak_t *)ctx;
    _z_session_weak_drop(session_weak);
    z_free(session_weak);
}

static z_result_t _ze_admin_space_undeclare_queryable(const z_loaned_session_t *zs, uint32_t queryable_id) {
    if (queryable_id == 0) {
        return _Z_RES_OK;
    }

    _z_queryable_t queryable = {
        ._entity_id = queryable_id,
        ._zn = _z_session_rc_clone_as_weak(zs),
    };
    if (_Z_RC_IS_NULL(&queryable._zn)) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }

    z_result_t ret = _z_undeclare_queryable(&queryable);
    _z_queryable_clear(&queryable);
    return ret;
}

#if Z_FEATURE_CONNECTIVITY == 1
static z_result_t _ze_admin_space_declare_session_queryable(const z_loaned_session_t *zs, uint32_t *out_queryable_id,
                                                            const z_loaned_keyexpr_t *ke, void *ctx) {
    z_owned_closure_query_t callback;
    z_result_t ret =
        z_closure_query(&callback, _ze_admin_space_session_query_handler, _ze_admin_space_query_dropper, ctx);
    if (ret != _Z_RES_OK) {
        _ze_admin_space_query_dropper(ctx);
        return ret;
    }

    _z_closure_query_t closure = callback._val;
    z_internal_closure_query_null(&callback);

    return _z_register_queryable(out_queryable_id, zs, ke, _Z_QUERYABLE_COMPLETE_DEFAULT, closure.call, closure.drop,
                                 closure.context, Z_LOCALITY_SESSION_LOCAL, NULL);
}
#endif

#if Z_FEATURE_CONNECTIVITY == 1 && Z_FEATURE_PUBLICATION == 1
typedef struct {
    _z_session_weak_t _session;
} _ze_admin_space_connectivity_listener_ctx_t;

static void _ze_admin_space_connectivity_listener_ctx_drop(void *ctx) {
    _ze_admin_space_connectivity_listener_ctx_t *listener_ctx = (_ze_admin_space_connectivity_listener_ctx_t *)ctx;
    if (listener_ctx != NULL) {
        _z_session_weak_drop(&listener_ctx->_session);
        z_free(listener_ctx);
    }
}

static _ze_admin_space_connectivity_listener_ctx_t *_ze_admin_space_connectivity_listener_ctx_new(
    const z_loaned_session_t *zs) {
    _ze_admin_space_connectivity_listener_ctx_t *ctx =
        (_ze_admin_space_connectivity_listener_ctx_t *)z_malloc(sizeof(_ze_admin_space_connectivity_listener_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->_session = _z_session_rc_clone_as_weak(zs);
    if (_Z_RC_IS_NULL(&ctx->_session)) {
        z_free(ctx);
        return NULL;
    }
    return ctx;
}

static z_result_t _ze_admin_space_undeclare_transport_listener(z_loaned_session_t *zs, size_t listener_id) {
    if (listener_id == 0) {
        return _Z_RES_OK;
    }

    z_owned_transport_events_listener_t listener;
    listener._val = (_z_transport_events_listener_t){0};
    listener._val._id = listener_id;
    listener._val._session = _z_session_rc_clone_as_weak(zs);
    if (_Z_RC_IS_NULL(&listener._val._session)) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    return z_undeclare_transport_events_listener(z_transport_events_listener_move(&listener));
}

static z_result_t _ze_admin_space_undeclare_link_listener(z_loaned_session_t *zs, size_t listener_id) {
    if (listener_id == 0) {
        return _Z_RES_OK;
    }

    z_owned_link_events_listener_t listener;
    listener._val = (_z_link_events_listener_t){0};
    listener._val._id = listener_id;
    listener._val._session = _z_session_rc_clone_as_weak(zs);
    if (_Z_RC_IS_NULL(&listener._val._session)) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }
    return z_undeclare_link_events_listener(z_link_events_listener_move(&listener));
}

static inline void _ze_admin_space_transport_listener_handle_clear(z_owned_transport_events_listener_t *listener) {
    _z_session_weak_drop(&listener->_val._session);
    _z_sync_group_drop(&listener->_val._callback_drop_sync_group);
    listener->_val = (_z_transport_events_listener_t){0};
}

static inline void _ze_admin_space_link_listener_handle_clear(z_owned_link_events_listener_t *listener) {
    _z_session_weak_drop(&listener->_val._session);
    _z_sync_group_drop(&listener->_val._callback_drop_sync_group);
    listener->_val = (_z_link_events_listener_t){0};
}

static z_result_t _ze_admin_space_put_session_local_json(_z_session_t *session, const z_loaned_keyexpr_t *ke,
                                                         z_owned_bytes_t *payload) {
    z_owned_encoding_t encoding;
    z_result_t ret = z_encoding_clone(&encoding, z_encoding_application_json());
    if (ret != _Z_RES_OK) {
        z_bytes_drop(z_bytes_move(payload));
        return ret;
    }

    ret = _z_write(session, ke, &payload->_val, &encoding._val, Z_SAMPLE_KIND_PUT,
                   z_internal_congestion_control_default_push(), Z_PRIORITY_DEFAULT, false, NULL, NULL,
                   Z_RELIABILITY_DEFAULT, NULL, Z_LOCALITY_SESSION_LOCAL);

    z_encoding_drop(z_encoding_move(&encoding));
    z_bytes_drop(z_bytes_move(payload));
    return ret;
}

static z_result_t _ze_admin_space_delete_session_local(_z_session_t *session, const z_loaned_keyexpr_t *ke) {
    return _z_write(session, ke, NULL, NULL, Z_SAMPLE_KIND_DELETE, z_internal_congestion_control_default_push(),
                    Z_PRIORITY_DEFAULT, false, NULL, NULL, Z_RELIABILITY_DEFAULT, NULL, Z_LOCALITY_SESSION_LOCAL);
}

static void _ze_admin_space_publish_transport_event(z_loaned_transport_event_t *event, void *ctx) {
    _ze_admin_space_connectivity_listener_ctx_t *listener_ctx = (_ze_admin_space_connectivity_listener_ctx_t *)ctx;
    if (event == NULL || listener_ctx == NULL) {
        return;
    }

    _z_session_rc_t session_rc = _z_session_weak_upgrade_if_open(&listener_ctx->_session);
    if (_Z_RC_IS_NULL(&session_rc)) {
        return;
    }

    const z_loaned_transport_t *transport = z_transport_event_transport(event);
    if (transport == NULL) {
        _z_session_rc_drop(&session_rc);
        return;
    }

    _z_session_t *session = _Z_RC_IN_VAL(&session_rc);
    z_id_t peer_zid = z_transport_zid(transport);

    z_view_keyexpr_t ke;
    _z_admin_space_ke_buf_t ke_buf;
    _ze_admin_space_session_transport_ke(&ke_buf, &session->_local_zid, &peer_zid, transport->_is_multicast);
    z_view_keyexpr_from_substr_unchecked(&ke, _z_admin_space_ke_buf_const_data(&ke_buf),
                                         _z_admin_space_ke_buf_size(&ke_buf));

    z_result_t ret = _Z_RES_OK;
    if (z_transport_event_kind(event) == Z_SAMPLE_KIND_PUT) {
        z_owned_bytes_t payload;
        ret = _ze_admin_space_encode_connectivity_transport_payload(&payload, &peer_zid, z_transport_whatami(transport),
                                                                    z_transport_is_qos(transport),
                                                                    z_transport_is_shm(transport));
        _Z_SET_IF_OK(ret, _ze_admin_space_put_session_local_json(session, z_view_keyexpr_loan(&ke), &payload));
    } else if (z_transport_event_kind(event) == Z_SAMPLE_KIND_DELETE) {
        ret = _ze_admin_space_delete_session_local(session, z_view_keyexpr_loan(&ke));
    }

    if (ret != _Z_RES_OK) {
        _Z_WARN("Failed to publish RFC connectivity transport event: %d", ret);
    }

    _z_session_rc_drop(&session_rc);
}

static void _ze_admin_space_publish_link_event(z_loaned_link_event_t *event, void *ctx) {
    _ze_admin_space_connectivity_listener_ctx_t *listener_ctx = (_ze_admin_space_connectivity_listener_ctx_t *)ctx;
    if (event == NULL || listener_ctx == NULL) {
        return;
    }

    _z_session_rc_t session_rc = _z_session_weak_upgrade_if_open(&listener_ctx->_session);
    if (_Z_RC_IS_NULL(&session_rc)) {
        return;
    }
    _z_session_t *session = _Z_RC_IN_VAL(&session_rc);

    const z_loaned_link_t *link = z_link_event_link(event);
    if (link == NULL) {
        _z_session_rc_drop(&session_rc);
        return;
    }
    z_id_t peer_zid = z_link_zid(link);

    z_view_keyexpr_t ke;
    _z_admin_space_ke_buf_t ke_buf;
    _ze_admin_space_session_link_ke(&ke_buf, &session->_local_zid, &peer_zid, _z_link_info_hash(link),
                                    !_z_string_is_empty(&link->_mcast_group));
    z_view_keyexpr_from_substr_unchecked(&ke, _z_admin_space_ke_buf_const_data(&ke_buf),
                                         _z_admin_space_ke_buf_size(&ke_buf));

    z_result_t ret = _Z_RES_OK;
    if (z_link_event_kind(event) == Z_SAMPLE_KIND_PUT) {
        z_owned_bytes_t payload;
        ret = _ze_admin_space_encode_connectivity_link_payload(&payload, link);
        _Z_SET_IF_OK(ret, _ze_admin_space_put_session_local_json(session, z_view_keyexpr_loan(&ke), &payload));
    } else if (z_link_event_kind(event) == Z_SAMPLE_KIND_DELETE) {
        ret = _ze_admin_space_delete_session_local(session, z_view_keyexpr_loan(&ke));
    }

    if (ret != _Z_RES_OK) {
        _Z_WARN("Failed to publish RFC connectivity link event: %d", ret);
    }
    _z_session_rc_drop(&session_rc);
}
#endif  // Z_FEATURE_CONNECTIVITY == 1 && Z_FEATURE_PUBLICATION == 1

z_result_t zp_start_admin_space(z_loaned_session_t *zs) {
    _Z_INFO("Starting admin space");
    _z_session_t *session = _Z_RC_IN_VAL(zs);
    z_id_t zid = z_info_zid(zs);
    z_result_t ret = _Z_RES_OK;
    _z_session_admin_space_mutex_lock(session);
    if (session->_admin_space_queryable_id != 0) {
        goto out;
    }

    _z_session_weak_t *pico_session_weak = _z_session_rc_clone_as_weak_ptr(zs);
    if (pico_session_weak == NULL || _Z_RC_IS_NULL(pico_session_weak)) {
        ret = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
        goto out;
    }

    z_view_keyexpr_t ke;
    _z_admin_space_ke_buf_t ke_buf;
    _ze_admin_space_pico_queryable_ke(&ke_buf, &zid, _Z_RC_IN_VAL(zs)->_mode);
    z_view_keyexpr_from_substr_unchecked(&ke, _z_admin_space_ke_buf_const_data(&ke_buf),
                                         _z_admin_space_ke_buf_size(&ke_buf));

    z_owned_closure_query_t callback;
    ret = z_closure_query(&callback, _ze_admin_space_pico_query_handler, _ze_admin_space_query_dropper,
                          pico_session_weak);
    if (ret != _Z_RES_OK) {
        _z_session_weak_drop(pico_session_weak);
        z_free(pico_session_weak);
        goto out;
    }

    z_owned_queryable_t admin_space_queryable;
    ret = z_declare_queryable(zs, &admin_space_queryable, z_view_keyexpr_loan(&ke), z_closure_query_move(&callback),
                              NULL);
    if (ret != _Z_RES_OK) {
        goto out;
    }

    session->_admin_space_queryable_id = admin_space_queryable._val._entity_id;
    _z_queryable_clear(&admin_space_queryable._val);

#if Z_FEATURE_CONNECTIVITY == 1
    _z_session_weak_t *session_session_weak = _z_session_rc_clone_as_weak_ptr(zs);
    if (_Z_RC_IS_NULL(session_session_weak)) {
        ret = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
        goto err_queryable;
    }

    _ze_admin_space_session_queryable_ke(&ke_buf, &zid);
    z_view_keyexpr_from_substr_unchecked(&ke, _z_admin_space_ke_buf_const_data(&ke_buf),
                                         _z_admin_space_ke_buf_size(&ke_buf));

    ret = _ze_admin_space_declare_session_queryable(zs, &session->_admin_space_session_queryable_id,
                                                    z_view_keyexpr_loan(&ke), session_session_weak);
    if (ret != _Z_RES_OK) {
        goto err_queryable;
    }

#if Z_FEATURE_PUBLICATION == 1
    session->_admin_space_transport_listener_id = 0;
    session->_admin_space_link_listener_id = 0;

    _ze_admin_space_connectivity_listener_ctx_t *transport_ctx = _ze_admin_space_connectivity_listener_ctx_new(zs);
    if (transport_ctx == NULL) {
        ret = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
        goto err_session_queryable;
    }

    z_owned_closure_transport_event_t transport_callback;
    ret = z_closure_transport_event(&transport_callback, _ze_admin_space_publish_transport_event,
                                    _ze_admin_space_connectivity_listener_ctx_drop, transport_ctx);
    if (ret != _Z_RES_OK) {
        _ze_admin_space_connectivity_listener_ctx_drop(transport_ctx);
        goto err_session_queryable;
    }

    z_owned_transport_events_listener_t transport_listener;
    z_transport_events_listener_options_t transport_opts;
    z_transport_events_listener_options_default(&transport_opts);
    ret = z_declare_transport_events_listener(zs, &transport_listener,
                                              z_closure_transport_event_move(&transport_callback), &transport_opts);
    if (ret != _Z_RES_OK) {
        goto err_session_queryable;
    }
    session->_admin_space_transport_listener_id = transport_listener._val._id;
    _ze_admin_space_transport_listener_handle_clear(&transport_listener);

    _ze_admin_space_connectivity_listener_ctx_t *link_ctx = _ze_admin_space_connectivity_listener_ctx_new(zs);
    if (link_ctx == NULL) {
        ret = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
        goto err_transport_listener;
    }

    z_owned_closure_link_event_t link_callback;
    ret = z_closure_link_event(&link_callback, _ze_admin_space_publish_link_event,
                               _ze_admin_space_connectivity_listener_ctx_drop, link_ctx);
    if (ret != _Z_RES_OK) {
        _ze_admin_space_connectivity_listener_ctx_drop(link_ctx);
        goto err_transport_listener;
    }

    z_owned_link_events_listener_t link_listener;
    z_link_events_listener_options_t link_opts;
    z_link_events_listener_options_default(&link_opts);
    ret = z_declare_link_events_listener(zs, &link_listener, z_closure_link_event_move(&link_callback), &link_opts);
    if (ret != _Z_RES_OK) {
        goto err_transport_listener;
    }
    session->_admin_space_link_listener_id = link_listener._val._id;
    _ze_admin_space_link_listener_handle_clear(&link_listener);
#endif
#endif

    ret = _Z_RES_OK;
    goto out;

#if Z_FEATURE_CONNECTIVITY == 1
#if Z_FEATURE_PUBLICATION == 1
err_transport_listener: {
    z_result_t undeclare_transport_listener_ret =
        _ze_admin_space_undeclare_transport_listener(zs, session->_admin_space_transport_listener_id);
    if (undeclare_transport_listener_ret == _Z_RES_OK) {
        session->_admin_space_transport_listener_id = 0;
    } else if (ret == _Z_RES_OK) {
        ret = undeclare_transport_listener_ret;
    }
}
#endif

err_session_queryable: {
    z_result_t undeclare_queryable_ret =
        _ze_admin_space_undeclare_queryable(zs, session->_admin_space_session_queryable_id);
    if (undeclare_queryable_ret == _Z_RES_OK) {
        session->_admin_space_session_queryable_id = 0;
    } else if (ret == _Z_RES_OK) {
        ret = undeclare_queryable_ret;
    }
}
#endif

#if Z_FEATURE_CONNECTIVITY == 1
err_queryable: {
    z_result_t undeclare_queryable_ret = _ze_admin_space_undeclare_queryable(zs, session->_admin_space_queryable_id);
    if (undeclare_queryable_ret == _Z_RES_OK) {
        session->_admin_space_queryable_id = 0;
    } else if (ret == _Z_RES_OK) {
        ret = undeclare_queryable_ret;
    }
}
#endif
out:
    _z_session_admin_space_mutex_unlock(session);
    return ret;
}

z_result_t zp_stop_admin_space(z_loaned_session_t *zs) {
    _Z_INFO("Stopping admin space");
    _z_session_t *session = _Z_RC_IN_VAL(zs);
    z_result_t ret = _Z_RES_OK;

    _z_session_admin_space_mutex_lock(session);
    uint32_t admin_space_queryable_id = session->_admin_space_queryable_id;
#if Z_FEATURE_CONNECTIVITY == 1
    uint32_t admin_space_session_queryable_id = session->_admin_space_session_queryable_id;
#if Z_FEATURE_PUBLICATION == 1
    size_t admin_space_transport_listener_id = session->_admin_space_transport_listener_id;
    size_t admin_space_link_listener_id = session->_admin_space_link_listener_id;

    if (admin_space_queryable_id == 0 && admin_space_session_queryable_id == 0 &&
        admin_space_transport_listener_id == 0 && admin_space_link_listener_id == 0) {
        goto out;
    }
#else
    if (admin_space_queryable_id == 0 && admin_space_session_queryable_id == 0) {
        goto out;
    }
#endif
#else
    if (admin_space_queryable_id == 0) {
        goto out;
    }
#endif

#if Z_FEATURE_CONNECTIVITY == 1 && Z_FEATURE_PUBLICATION == 1
    if (admin_space_transport_listener_id != 0) {
        z_result_t listener_ret = _ze_admin_space_undeclare_transport_listener(zs, admin_space_transport_listener_id);
        if (listener_ret == _Z_RES_OK) {
            session->_admin_space_transport_listener_id = 0;
        } else if (ret == _Z_RES_OK) {
            ret = listener_ret;
        }
    }

    if (admin_space_link_listener_id != 0) {
        z_result_t listener_ret = _ze_admin_space_undeclare_link_listener(zs, admin_space_link_listener_id);
        if (listener_ret == _Z_RES_OK) {
            session->_admin_space_link_listener_id = 0;
        } else if (ret == _Z_RES_OK) {
            ret = listener_ret;
        }
    }
#endif

#if Z_FEATURE_CONNECTIVITY == 1
    if (admin_space_session_queryable_id != 0) {
        z_result_t queryable_ret = _ze_admin_space_undeclare_queryable(zs, admin_space_session_queryable_id);
        if (queryable_ret == _Z_RES_OK) {
            session->_admin_space_session_queryable_id = 0;
        } else if (ret == _Z_RES_OK) {
            ret = queryable_ret;
        }
    }
#endif

    if (admin_space_queryable_id != 0) {
        z_result_t queryable_ret = _ze_admin_space_undeclare_queryable(zs, admin_space_queryable_id);
        if (queryable_ret == _Z_RES_OK) {
            session->_admin_space_queryable_id = 0;
        } else if (ret == _Z_RES_OK) {
            ret = queryable_ret;
        }
    }

out:
    _z_session_admin_space_mutex_unlock(session);
    return ret;
}

#endif  // Z_FEATURE_ADMIN_SPACE == 1
