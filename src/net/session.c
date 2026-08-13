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
//   Błażej Sowa, <blazej@fictionlab.pl>

#include "zenoh-pico/net/session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/protocol/definitions/declarations.h"
#include "zenoh-pico/protocol/definitions/network.h"
#include "zenoh-pico/runtime/runtime.h"
#include "zenoh-pico/session/interest.h"
#include "zenoh-pico/session/utils.h"
#include "zenoh-pico/transport/connectivity.h"
#include "zenoh-pico/transport/multicast/tx.h"
#include "zenoh-pico/transport/peers.h"
#include "zenoh-pico/transport/rx.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/tx.h"
#include "zenoh-pico/utils/config.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/result.h"
#include "zenoh-pico/utils/sleep.h"
#include "zenoh-pico/utils/string.h"
#include "zenoh-pico/utils/uuid.h"

#if Z_FEATURE_SCOUTING == 1
static z_result_t _z_locators_by_scout(const _z_config_t *config, const _z_id_t *zid, _z_string_svec_t *locators) {
    z_result_t ret = _Z_RES_OK;

    z_what_t what = config->_scouting_what._parsed;

    _z_string_t mcast_locator = _z_string_alias_str(config->_multicast_locator);

    uint32_t timeout = (uint32_t)config->_scouting_timeout._parsed;

    // Scout and return upon the first result
    _z_hello_slist_t *hellos = _z_scout_inner(what, *zid, &mcast_locator, timeout, true);
    if (hellos != NULL) {
        _z_hello_t *hello = _z_hello_slist_value(hellos);
        _z_string_svec_copy(locators, &hello->_locators, true);
    }
    _z_hello_slist_free(&hellos);
    return ret;
}

#if Z_FEATURE_UNICAST_TRANSPORT == 1
z_result_t _z_connect_first_via_scout(_z_session_t *session) {
    _z_string_svec_t locators = _z_string_svec_null();
    _Z_RETURN_IF_ERR(_z_locators_by_scout(&session->_config, &session->_local_zid, &locators));
    size_t locators_len = _z_string_svec_len(&locators);
    z_result_t ret = _Z_ERR_SCOUT_NO_RESULTS;
    for (size_t i = 0; i < locators_len; i++) {
        _z_string_t *locator = _z_string_svec_get(&locators, i);
        _Z_DEBUG("Trying to connect to scouted locator: "_Z_STRING_PRINT_FORMAT, _Z_STRING_PRINT_ARG(locator));
        if (_z_transport_manager_connect_peer(&session->_transport_manager, locator, _ZP_CONNECT_PEER_ID_FROM_SCOUT) ==
            _Z_RES_OK) {
            _z_string_svec_clear(&locators);
            ret = _Z_RES_OK;
            break;
        }
    }
    _z_string_svec_clear(&locators);
    return ret;
}
#endif  // Z_FEATURE_UNICAST_TRANSPORT == 1
#endif  // Z_FEATURE_SCOUTING == 1

#if Z_FEATURE_UNICAST_TRANSPORT == 1
z_result_t _z_connect_via_connect_locators(_z_session_t *session) {
    z_clock_t now = z_clock_now();
    uint32_t sleep_delay_ms = 0;
    z_result_t ret = session->_config._mode._parsed == Z_WHATAMI_CLIENT ? _Z_ERR_TRANSPORT_OPEN_FAILED : _Z_RES_OK;
    if (_z_config_connect_vec_is_empty(&session->_config._connect)) {
        return ret;
    }
    do {
        if (sleep_delay_ms > 0) {
            z_sleep_ms((size_t)sleep_delay_ms);
        }
        for (size_t i = 0; i < _z_config_connect_vec_size(&session->_config._connect); i++) {
            const char *locator_str = *_z_config_connect_vec_at(&session->_config._connect, i);
            _z_string_view_t lv = _z_string_view_make_from_str(locator_str);
            if (_z_transport_manager_connect_peer(&session->_transport_manager, _z_string_view_deref(&lv),
                                                  (_z_connect_peer_id_t)i) == _Z_RES_OK) {
                if (session->_config._mode._parsed == Z_WHATAMI_CLIENT) {
                    return _Z_RES_OK;
                }
            } else if (session->_config._connect_exit_on_failure._parsed) {
                return _Z_ERR_TRANSPORT_OPEN_FAILED;
            }
            if (session->_config._connect_timeout._parsed > 0 &&
                (unsigned long)session->_config._connect_timeout._parsed <= z_clock_elapsed_ms(&now)) {
                return ret;
            }
        }
        if (session->_config._mode._parsed == Z_WHATAMI_PEER) {
            break;  // In peer mode, we only attempt to connect once to each locator
        }
        sleep_delay_ms = _z_connect_attempt_duration_increase(sleep_delay_ms, _ZP_CONNECT_ATTEMPT_MIN_INTERVAL_MS,
                                                              _ZP_CONNECT_ATTEMPT_MAX_INTERVAL_MS);
    } while ((unsigned long)session->_config._connect_timeout._parsed >
             z_clock_elapsed_ms(&now) + (unsigned long)sleep_delay_ms);
    return ret;
}
#endif

/**
 * Open transports based on the configured listen and connect locators.
 *
 * - In peer mode:
 *     - If listen locators are provided, they are all attempted to connect synchronously.
 *     - Connect locators are attempt once each, and the rest is handled by the transport manager's background tasks.
 *
 * - In client mode:
 *     - Atempts to connect to connect to each connect locator in sequence until one succeeds or all fail.
 *     - If no locators are provided, multicast scouting is attempted if enabled.
 *
 * Returns:
 * - _Z_RES_OK on success (one locator is connected for client, listen locators are established based on peer policy).
 * - An error otherwise.
 */
z_result_t _z_open(_z_session_rc_t *zn) {
    _z_session_t *session = _Z_RC_IN_VAL(zn);
    if (session->_config._mode._parsed == Z_WHATAMI_CLIENT) {
#if Z_FEATURE_UNICAST_TRANSPORT == 1
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
        if (!_z_config_listen_vec_is_empty(&session->_config._listen)) {
            _Z_ERROR("Client mode cannot have listen locators configured");
            return _Z_ERR_INVALID;
        }
#endif
        if (!_z_config_connect_vec_is_empty(&session->_config._connect)) {
            _Z_RETURN_IF_ERR(_z_connect_via_connect_locators(session));
#if Z_FEATURE_SCOUTING == 1
        } else if (session->_config._multicast_scouting._parsed) {
            _Z_RETURN_IF_ERR(_z_connect_first_via_scout(session));
#endif
        } else {
            _Z_ERROR("Client mode requires at least one connect locator or multicast scouting enabled");
            return _Z_ERR_INVALID;
        }
#else
        _Z_ERROR("Client mode requires unicast transport feature enabled");
        return _Z_ERR_INVALID;
#endif
    } else if (session->_config._mode._parsed == Z_WHATAMI_PEER) {
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
        size_t listen_count = 0;
        for (size_t i = 0; i < _z_config_listen_vec_size(&session->_config._listen); i++) {
            const char *locator = *_z_config_listen_vec_at(&session->_config._listen, i);
            _z_string_view_t lv = _z_string_view_make_from_str(locator);
            if (_z_transport_manager_add_listener(&session->_transport_manager, _z_string_view_deref(&lv),
                                                  (_z_listen_listener_id_t)i) != _Z_RES_OK) {
                _Z_ERROR("Failed to add listener for locator: %s", locator);
                if (session->_config._listen_exit_on_failure._parsed) {
                    return _Z_ERR_TRANSPORT_OPEN_FAILED;
                }
            } else {
                listen_count++;
            }
        }
#if Z_FEATURE_UNICAST_TRANSPORT == 0
        if (listen_count == 0) {
            _Z_ERROR("Multicast peer mode requires at least one listen locator to be successfully added");
            return _Z_ERR_TRANSPORT_OPEN_FAILED;
        }
#else
        if (listen_count == 0 && _z_config_connect_vec_is_empty(&session->_config._connect)) {
            _Z_ERROR("Peer mode requires at least one listen locator or connect locator to be successfully added");
            return _Z_ERR_TRANSPORT_OPEN_FAILED;
        }
        _Z_RETURN_IF_ERR(_z_connect_via_connect_locators(session));
        // remaining connect locators and scouting will be handled by the transport manager's background task
#endif
#else
        _Z_ERROR("Peer mode requires unicast peer or multicast transport feature enabled");
        return _Z_ERR_INVALID;
#endif
    }
    return _z_transport_manager_spawn_tasks(&session->_transport_manager);
}

bool _z_session_is_closed(const _z_session_t *session) {
    return _z_atomic_bool_load((_z_atomic_bool_t *)&session->_is_closed, _z_memory_order_acquire);
}

_z_session_rc_t _z_session_weak_upgrade_if_open(const _z_session_weak_t *weak) {
    _z_session_rc_t sess_rc = _z_session_weak_upgrade(weak);
    if (!_Z_RC_IS_NULL(&sess_rc) && _z_session_is_closed(_Z_RC_IN_VAL(&sess_rc))) {
        _z_session_rc_drop(&sess_rc);
    }
    return sess_rc;
}

z_result_t _zp_read(_z_session_t *zn, bool single_read) {
    _ZP_UNUSED(single_read);
    return _z_transport_manager_read(&zn->_transport_manager);
}

z_result_t _zp_send_keep_alive(_z_session_t *zn) {
    return _z_transport_manager_send_keep_alive(&zn->_transport_manager);
}

z_result_t _zp_send_join(_z_session_t *zn) {
#if Z_FEATURE_MULTICAST_TRANSPORT == 1
    return _z_multicast_transport_manager_send_join(&zn->_transport_manager._multicast);
#else
    _ZP_UNUSED(zn);
    return _Z_ERR_INVALID;
#endif
}
