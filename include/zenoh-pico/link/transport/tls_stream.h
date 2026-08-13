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

#ifndef ZENOH_PICO_LINK_TRANSPORT_TLS_STREAM_H
#define ZENOH_PICO_LINK_TRANSPORT_TLS_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/config.h"
#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_LINK_TLS == 1

#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

typedef struct {
    mbedtls_ssl_context _ssl;
    mbedtls_ssl_config _ssl_config;
    mbedtls_entropy_context _entropy;
    mbedtls_hmac_drbg_context _hmac_drbg;
    mbedtls_x509_crt _ca_cert;
    mbedtls_pk_context _listen_key;
    mbedtls_x509_crt _listen_cert;
    mbedtls_pk_context _client_key;
    mbedtls_x509_crt _client_cert;
    bool _enable_mtls;
    int _fd;  // put socket fd here since mbedtls stores the address of the socket desscriptor
} _z_tls_context_t;

z_result_t _z_open_tls(_z_tls_context_t **ctx, _z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t *rep,
                       const char *hostname, const _z_str_intmap_t *config);
z_result_t _z_listen_tls(_z_tls_context_t **ctx, _z_sys_net_socket_t *tcp_sock, const _z_sys_net_endpoint_t *rep,
                         const _z_str_intmap_t *config);
z_result_t _z_tls_accept(const _z_tls_context_t *listen_ctx, const _z_sys_net_socket_t *listen_sock,
                         _z_tls_context_t **ctx, _z_sys_net_socket_t *socket);
void _z_close_tls(_z_tls_context_t **ctx);
size_t _z_read_tls(_z_tls_context_t *ctx, uint8_t *ptr, size_t len);
size_t _z_write_tls(_z_tls_context_t *ctx, const uint8_t *ptr, size_t len);

_z_str_intmap_t _z_tls_merge_config(_z_str_intmap_t *endpoint_cfg, const _z_config_t *session_cfg);
z_result_t _z_endpoint_tls_valid(const _z_endpoint_t *ep);
typedef struct _z_unicast_link_tls_t {
    _z_tls_context_t *_tls_ctx;
    _z_sys_net_socket_t _sock;
} _z_unicast_link_tls_t;

typedef struct _z_unicast_listener_tls_t {
    _z_tls_context_t *_tls_ctx;
    _z_sys_net_socket_t _sock;
} _z_unicast_listener_tls_t;

bool _z_unicast_link_tls_read(_z_unicast_link_tls_t *tls, uint8_t *ptr, size_t *len);
bool _z_unicast_link_tls_write(_z_unicast_link_tls_t *tls, const uint8_t *ptr, size_t *len);
z_result_t _z_unicast_listener_tls_create(_z_unicast_listener_tls_t *listener, const _z_endpoint_t *endpoint);
z_result_t _z_unicast_listener_tls_accept(_z_unicast_listener_tls_t *listener, _z_unicast_link_tls_t *link);
z_result_t _z_unicast_link_tls_create(_z_unicast_link_tls_t *tls, const _z_endpoint_t *endpoint);
void _z_unicast_link_tls_clear(_z_unicast_link_tls_t *tls);
void _z_unicast_listener_tls_clear(_z_unicast_listener_tls_t *listener);
uint16_t _z_unicast_link_tls_get_mtu(const _z_unicast_link_tls_t *tls);
bool _z_unicast_link_tls_is_reliable(const _z_unicast_link_tls_t *tls);
bool _z_unicast_link_tls_is_streamed(const _z_unicast_link_tls_t *tls);
z_result_t _z_unicast_link_tls_get_endpoints(const _z_unicast_link_tls_t *tls, char *local, size_t local_len,
                                             char *remote, size_t remote_len);
_z_sys_net_socket_t *_z_unicast_link_tls_get_sock(_z_unicast_link_tls_t *tls);
#endif  // Z_FEATURE_LINK_TLS == 1

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_TRANSPORT_TLS_STREAM_H */
