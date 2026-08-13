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

#include "zenoh-pico/link/config/tls.h"

#include <stddef.h>
#include <string.h>

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/transport/tcp.h"
#include "zenoh-pico/link/transport/tls_stream.h"
#include "zenoh-pico/utils/config.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/string.h"

#if Z_FEATURE_LINK_TLS == 1

z_result_t _z_endpoint_tls_valid(const _z_endpoint_t *endpoint) {
    _z_string_t tls_str = _z_string_alias_str(TLS_SCHEMA);
    if (!_z_string_equals(&endpoint->_locator._protocol, &tls_str)) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
        return _Z_ERR_CONFIG_LOCATOR_INVALID;
    }

    z_result_t ret = _z_tcp_address_valid(&endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_INVALID);
    }
    return ret;
}

_z_str_intmap_t _z_tls_merge_config(_z_str_intmap_t *endpoint_cfg, const _z_config_t *session_cfg) {
    _z_str_intmap_t cfg;
    if (endpoint_cfg != NULL) {
        _z_str_intmap_move(&cfg, endpoint_cfg);
    } else {
        cfg = _z_str_intmap_make();
    }
    if (session_cfg == NULL) {
        return cfg;
    }
    static const struct {
        uint8_t locator_key;
        uint8_t session_key;
    } mapping[] = {{TLS_CONFIG_ROOT_CA_CERTIFICATE_KEY, Z_CONFIG_TLS_ROOT_CA_CERTIFICATE_KEY},
                   {TLS_CONFIG_ROOT_CA_CERTIFICATE_BASE64_KEY, Z_CONFIG_TLS_ROOT_CA_CERTIFICATE_BASE64_KEY},
                   {TLS_CONFIG_LISTEN_PRIVATE_KEY_KEY, Z_CONFIG_TLS_LISTEN_PRIVATE_KEY_KEY},
                   {TLS_CONFIG_LISTEN_PRIVATE_KEY_BASE64_KEY, Z_CONFIG_TLS_LISTEN_PRIVATE_KEY_BASE64_KEY},
                   {TLS_CONFIG_LISTEN_CERTIFICATE_KEY, Z_CONFIG_TLS_LISTEN_CERTIFICATE_KEY},
                   {TLS_CONFIG_LISTEN_CERTIFICATE_BASE64_KEY, Z_CONFIG_TLS_LISTEN_CERTIFICATE_BASE64_KEY},
                   {TLS_CONFIG_ENABLE_MTLS_KEY, Z_CONFIG_TLS_ENABLE_MTLS_KEY},
                   {TLS_CONFIG_CONNECT_PRIVATE_KEY_KEY, Z_CONFIG_TLS_CONNECT_PRIVATE_KEY_KEY},
                   {TLS_CONFIG_CONNECT_PRIVATE_KEY_BASE64_KEY, Z_CONFIG_TLS_CONNECT_PRIVATE_KEY_BASE64_KEY},
                   {TLS_CONFIG_CONNECT_CERTIFICATE_KEY, Z_CONFIG_TLS_CONNECT_CERTIFICATE_KEY},
                   {TLS_CONFIG_CONNECT_CERTIFICATE_BASE64_KEY, Z_CONFIG_TLS_CONNECT_CERTIFICATE_BASE64_KEY},
                   {TLS_CONFIG_VERIFY_NAME_ON_CONNECT_KEY, Z_CONFIG_TLS_VERIFY_NAME_ON_CONNECT_KEY}};

    for (size_t i = 0; i < sizeof(mapping) / sizeof(mapping[0]); i++) {
        if (_z_str_intmap_get(&cfg, mapping[i].locator_key) != NULL) {
            continue;
        }
        const char *value = _z_config_get(session_cfg, mapping[i].session_key);
        if (value != NULL) {
            _z_str_intmap_insert(&cfg, mapping[i].locator_key, _z_str_clone(value));
        }
    }
    return cfg;
}

bool _z_unicast_link_tls_read(_z_unicast_link_tls_t *tls, uint8_t *ptr, size_t *len) {
    *len = _z_read_tls(tls->_tls_ctx, ptr, *len);
    return *len != SIZE_MAX;
}

bool _z_unicast_link_tls_write(_z_unicast_link_tls_t *tls, const uint8_t *ptr, size_t *len) {
    *len = _z_write_tls(tls->_tls_ctx, ptr, *len);
    return *len != SIZE_MAX;
}

z_result_t _z_unicast_listener_tls_create(_z_unicast_listener_tls_t *listener, const _z_endpoint_t *endpoint) {
    memset(listener, 0, sizeof(_z_unicast_listener_tls_t));
    z_result_t ret = _Z_RES_OK;

    char *host = _z_tcp_address_parse_host(&endpoint->_locator._address);
    if (host == NULL) {
        _Z_ERROR("Invalid TLS endpoint");
        z_free(host);
        return _Z_ERR_GENERIC;
    }

    _z_sys_net_endpoint_t rep = {0};
    ret = _z_tcp_endpoint_init_from_address(&rep, &endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        z_free(host);
        return ret;
    }

    ret = _z_listen_tls(&listener->_tls_ctx, &listener->_sock, &rep, &endpoint->_config);
    _z_tcp_endpoint_clear(&rep);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("TLS listen failed");
    } else {
        _z_socket_set_blocking(&listener->_sock, false);
    }

    z_free(host);
    return ret;
}

z_result_t _z_unicast_listener_tls_accept(_z_unicast_listener_tls_t *listener, _z_unicast_link_tls_t *link) {
    memset(link, 0, sizeof(_z_unicast_link_tls_t));
    _z_socket_set_blocking(&listener->_sock, false);
    z_result_t ret = _z_tcp_accept(&listener->_sock, &link->_sock);
    if (ret != _Z_RES_OK) {
        return ret;
    }
    ret = _z_tls_accept(listener->_tls_ctx, &listener->_sock, &link->_tls_ctx, &link->_sock);
    _z_socket_set_blocking(&listener->_sock, true);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("TLS accept failed");
        _z_tcp_close(&link->_sock);
    } else {
        _z_socket_set_blocking(&link->_sock, true);
    }
    return ret;
}

z_result_t _z_unicast_link_tls_create(_z_unicast_link_tls_t *tls, const _z_endpoint_t *endpoint) {
    memset(tls, 0, sizeof(_z_unicast_link_tls_t));
    char *hostname = _z_tcp_address_parse_host(&endpoint->_locator._address);
    if (hostname == NULL) {
        _Z_ERROR("Failed to parse TLS endpoint address");
        z_free(hostname);
        return _Z_ERR_GENERIC;
    }

    _z_sys_net_endpoint_t rep = {0};
    z_result_t ret = _z_tcp_endpoint_init_from_address(&rep, &endpoint->_locator._address);
    if (ret != _Z_RES_OK) {
        z_free(hostname);
        return ret;
    }

    ret = _z_open_tls(&tls->_tls_ctx, &tls->_sock, &rep, hostname, &endpoint->_config);
    _z_tcp_endpoint_clear(&rep);
    z_free(hostname);
    if (ret != _Z_RES_OK) {
        _Z_ERROR("TLS open failed");
    } else {
        _z_socket_set_blocking(&tls->_sock, true);
    }
    return ret;
}

void _z_unicast_link_tls_clear(_z_unicast_link_tls_t *tls) {
    _z_close_tls(&tls->_tls_ctx);
    _z_tcp_close(&tls->_sock);
}

void _z_unicast_listener_tls_clear(_z_unicast_listener_tls_t *listener) {
    _z_close_tls(&listener->_tls_ctx);
    _z_tcp_close(&listener->_sock);
}

uint16_t _z_unicast_link_tls_get_mtu(const _z_unicast_link_tls_t *tls) {
    _ZP_UNUSED(tls);
    return 65535;  // Maximum MTU for TLS
}

bool _z_unicast_link_tls_is_reliable(const _z_unicast_link_tls_t *tls) {
    _ZP_UNUSED(tls);
    return true;
}

bool _z_unicast_link_tls_is_streamed(const _z_unicast_link_tls_t *tls) {
    _ZP_UNUSED(tls);
    return true;
}

z_result_t _z_unicast_link_tls_get_endpoints(const _z_unicast_link_tls_t *tls, char *local, size_t local_len,
                                             char *remote, size_t remote_len) {
    return _z_socket_get_endpoints(&tls->_sock, local, local_len, remote, remote_len);
}

_z_sys_net_socket_t *_z_unicast_link_tls_get_sock(_z_unicast_link_tls_t *tls) { return &tls->_sock; }

#endif  // Z_FEATURE_LINK_TLS == 1
