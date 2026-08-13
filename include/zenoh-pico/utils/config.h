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

#ifndef ZENOH_PICO_UTILS_PROPERTY_H
#define ZENOH_PICO_UTILS_PROPERTY_H

#include <stdbool.h>
#include <stdint.h>

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/core.h"
#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_UNICAST_TRANSPORT == 1
#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_TYPE const char *
#define _ZP_STATIC_VECTOR_TEMPLATE_NAME _z_config_connect_vec
#define _ZP_STATIC_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_UNICAST_PEERS
#include "zenoh-pico/collections/static_vector_template.h"
#endif

#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_TYPE const char *
#define _ZP_STATIC_VECTOR_TEMPLATE_NAME _z_config_listen_vec
#define _ZP_STATIC_VECTOR_TEMPLATE_SIZE Z_MAX_NUM_LISTENERS
#include "zenoh-pico/collections/static_vector_template.h"
#endif

typedef struct {
    const char *_str;
    bool _parsed;
} _z_config_bool_t;

typedef struct {
    const char *_str;
    uint32_t _parsed;
} _z_config_uint_t;

typedef struct {
    const char *_str;
    z_whatami_t _parsed;
} _z_config_mode_t;

typedef struct {
    const char *_str;
    z_what_t _parsed;
} _z_config_what_t;

typedef struct {
    const char *_str;
    _z_id_t _parsed;
} _z_config_zid_t;

/**
 * Zenoh-net configuration represented as a struct with one dedicated field per property.
 *
 * Plain string properties are stored as non-owning pointers to a null-terminated C
 * string (typically a compile-time constant) and are `NULL` when the corresponding
 * property is not set. Typed properties (boolean, integer, mode, what, zid) keep the
 * original string in their `_str` member (also non-owning, `NULL` when unset) alongside
 * the parsed value, which is validated at insert time and pre-filled with its default
 * by `_z_config_init`. The configuration does not take ownership of the strings, so no
 * copying or freeing is performed. The `_connect` property accepts multiple values and
 * is therefore stored as a fixed-capacity vector of strings.
 */
typedef struct {
    // Session properties
    _z_config_mode_t _mode;
#if Z_FEATURE_UNICAST_TRANSPORT == 1
    _z_config_connect_vec_t _connect;
    _z_config_uint_t _connect_timeout;
    _z_config_bool_t _connect_exit_on_failure;
#endif
#if Z_FEATURE_UNICAST_PEER == 1 || Z_FEATURE_MULTICAST_TRANSPORT == 1
    _z_config_listen_vec_t _listen;
    _z_config_bool_t _listen_exit_on_failure;
#endif
    const char *_user;
    const char *_password;
    _z_config_bool_t _multicast_scouting;
    const char *_multicast_locator;
    _z_config_uint_t _scouting_timeout;
    _z_config_what_t _scouting_what;
    _z_config_zid_t _session_zid;
    _z_config_bool_t _add_timestamp;
    // TLS properties
    const char *_tls_root_ca_certificate;
    const char *_tls_root_ca_certificate_base64;
    const char *_tls_listen_private_key;
    const char *_tls_listen_private_key_base64;
    const char *_tls_listen_certificate;
    const char *_tls_listen_certificate_base64;
    _z_config_bool_t _tls_enable_mtls;
    const char *_tls_connect_private_key;
    const char *_tls_connect_private_key_base64;
    const char *_tls_connect_certificate;
    const char *_tls_connect_certificate_base64;
    _z_config_bool_t _tls_verify_name_on_connect;
} _z_config_t;

/**
 * Initialize a new empty configuration.
 */
void _z_config_init(_z_config_t *ps);

/**
 * Insert a property with a given key into the configuration.
 * If a property with the same key already exists, it is replaced.
 *
 * The configuration stores @p value as a non-owning pointer, so the caller must
 * ensure the referenced string outlives the configuration (typically a
 * compile-time string constant).
 *
 * Parameters:
 *   ps: A pointer to the configuration.
 *   key: The key of the property to add.
 *   value: The value of the property to add.
 */
z_result_t _zp_config_insert(_z_config_t *ps, uint8_t key, const char *value);

/**
 * Get the property with the given key from the configuration.
 *
 * Parameters:
 *     ps: A pointer to the configuration.
 *     key: The key of the property.
 *
 * Returns:
 *     The non-owning value of the property with key ``key`` in configuration ``ps``.
 */
const char *_z_config_get(const _z_config_t *ps, uint8_t key);

/**
 * Check if a configuration is empty.
 *
 * Parameters:
 *   ps: A pointer to the configuration.
 *
 * Returns:
 *   true if the configuration is empty, false otherwise.
 */
bool _z_config_is_empty(const _z_config_t *ps);
/**
 * Clone a config.
 *
 * Parameters:
 *     dst: A pointer to the configuration to initialize with the clone.
 *     src: A pointer to the configuration to clone.
 */
z_result_t _z_config_copy(_z_config_t *dst, const _z_config_t *src);
_z_config_t _z_config_clone(const _z_config_t *src);

/**
 * Move a configuration, leaving the source empty.
 */
z_result_t _z_config_move(_z_config_t *dst, _z_config_t *src);

/**
 * Clear a configuration, resetting it to an empty state.
 *
 * Configuration strings are non-owning, so no memory is released.
 *
 * Parameters:
 *   ps: A pointer to the configuration.
 */
void _z_config_clear(_z_config_t *ps);

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_UTILS_PROPERTY_H */
