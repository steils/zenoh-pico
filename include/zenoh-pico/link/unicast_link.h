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

#ifndef ZENOH_PICO_UNICAST_LINK_H
#define ZENOH_PICO_UNICAST_LINK_H

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/config.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_UNICAST_TRANSPORT == 1

#if Z_FEATURE_LINK_TCP == 1
#include "zenoh-pico/link/transport/tcp.h"
#endif

#if Z_FEATURE_LINK_UDP_UNICAST == 1
#include "zenoh-pico/link/transport/udp_unicast.h"
#endif

#if Z_FEATURE_LINK_WS == 1
#include "zenoh-pico/link/transport/ws.h"
#endif

#if Z_FEATURE_LINK_BLUETOOTH == 1
#include "zenoh-pico/link/transport/bt.h"
#endif

#if Z_FEATURE_LINK_SERIAL == 1
#include "zenoh-pico/link/transport/serial_protocol.h"
#endif

#if Z_FEATURE_LINK_TLS == 1
#include "zenoh-pico/link/transport/tls_stream.h"
#endif

#include "zenoh-pico/utils/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _ZP_VARIANT_TEMPLATE_NAME _z_unicast_link_inner
#if Z_FEATURE_LINK_TCP == 1
#define _ZP_VARIANT_TEMPLATE_1_TYPE _z_unicast_link_tcp_t
#define _ZP_VARIANT_TEMPLATE_1_NAME tcp
#define _ZP_VARIANT_TEMPLATE_1_DESTROY_FN _z_unicast_link_tcp_clear
#define _ZP_UNICAST_LINK_CASE_TCP(f) (tcp, f),
#else
#define _ZP_UNICAST_LINK_CASE_TCP(f)
#endif
#if Z_FEATURE_LINK_UDP_UNICAST == 1
#define _ZP_VARIANT_TEMPLATE_2_TYPE _z_unicast_link_udp_t
#define _ZP_VARIANT_TEMPLATE_2_NAME udp
#define _ZP_VARIANT_TEMPLATE_2_DESTROY_FN _z_unicast_link_udp_clear
#define _ZP_UNICAST_LINK_CASE_UDP(f) (udp, f),
#else
#define _ZP_UNICAST_LINK_CASE_UDP(f)
#endif
#if Z_FEATURE_LINK_BLUETOOTH == 1
#define _ZP_VARIANT_TEMPLATE_3_TYPE _z_unicast_link_bt_t
#define _ZP_VARIANT_TEMPLATE_3_NAME bt
#define _ZP_VARIANT_TEMPLATE_3_DESTROY_FN _z_unicast_link_bt_clear
#define _ZP_UNICAST_LINK_CASE_BT(f) (bt, f),
#else
#define _ZP_UNICAST_LINK_CASE_BT(f)
#endif
#if Z_FEATURE_LINK_SERIAL == 1
#define _ZP_VARIANT_TEMPLATE_4_TYPE _z_unicast_link_serial_t
#define _ZP_VARIANT_TEMPLATE_4_NAME serial
#define _ZP_VARIANT_TEMPLATE_4_DESTROY_FN _z_unicast_link_serial_clear
#define _ZP_UNICAST_LINK_CASE_SERIAL(f) (serial, f),
#else
#define _ZP_UNICAST_LINK_CASE_SERIAL(f)
#endif
#if Z_FEATURE_LINK_WS == 1
#define _ZP_VARIANT_TEMPLATE_5_TYPE _z_unicast_link_ws_t
#define _ZP_VARIANT_TEMPLATE_5_NAME ws
#define _ZP_VARIANT_TEMPLATE_5_DESTROY_FN _z_unicast_link_ws_clear
#define _ZP_UNICAST_LINK_CASE_WS(f) (ws, f),
#else
#define _ZP_UNICAST_LINK_CASE_WS(f)
#endif
#if Z_FEATURE_LINK_TLS == 1
#define _ZP_VARIANT_TEMPLATE_6_TYPE _z_unicast_link_tls_t
#define _ZP_VARIANT_TEMPLATE_6_NAME tls
#define _ZP_VARIANT_TEMPLATE_6_DESTROY_FN _z_unicast_link_tls_clear
#define _ZP_UNICAST_LINK_CASE_TLS(f) (tls, f),
#else
#define _ZP_UNICAST_LINK_CASE_TLS(f)
#endif
#include "zenoh-pico/collections/variant_template.h"

typedef struct _z_unicast_link_t {
    _z_unicast_link_inner_t _inner;
} _z_unicast_link_t;

static inline _z_unicast_link_t _z_unicast_link_null(void) {
    _z_unicast_link_t link;
    link._inner = _z_unicast_link_inner_none();
    return link;
}

// endpoint is not consumed upon failure
static inline z_result_t _z_unicast_link_create(_z_unicast_link_t *link, _z_endpoint_t *endpoint,
                                                const _z_config_t *config) {
    _ZP_UNUSED(config);
    link->_inner = _z_unicast_link_inner_none();
    z_result_t ret = _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
    if (false) {
#if Z_FEATURE_LINK_TCP == 1
    } else if (_z_endpoint_tcp_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_unicast_link_inner_tag_tcp;
        ret = _z_unicast_link_tcp_create(&link->_inner._tcp, endpoint);
#endif
#if Z_FEATURE_LINK_UDP_UNICAST == 1
    } else if (_z_endpoint_udp_unicast_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_unicast_link_inner_tag_udp;
        ret = _z_unicast_link_udp_create(&link->_inner._udp, endpoint);
#endif
#if Z_FEATURE_LINK_BLUETOOTH == 1
    } else if (_z_endpoint_bt_unicast_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_unicast_link_inner_tag_bt;
        ret = _z_unicast_link_bt_create(&link->_inner._bt, endpoint);
#endif
#if Z_FEATURE_LINK_SERIAL == 1
    } else if (_z_endpoint_serial_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_unicast_link_inner_tag_serial;
        ret = _z_unicast_link_serial_create(&link->_inner._serial, endpoint);
#endif
#if Z_FEATURE_LINK_WS == 1
    } else if (_z_endpoint_ws_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_unicast_link_inner_tag_ws;
        ret = _z_unicast_link_ws_create(&link->_inner._ws, endpoint);
#endif
#if Z_FEATURE_LINK_TLS == 1
    } else if (_z_endpoint_tls_valid(endpoint) == Z_OK) {
        endpoint->_config = _z_tls_merge_config(&endpoint->_config, config);
        link->_inner._tag = _z_unicast_link_inner_tag_tls;
        ret = _z_unicast_link_tls_create(&link->_inner._tls, endpoint);
#endif
    }
    if (ret != Z_OK) {
        *link = _z_unicast_link_null();
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN);
    } else {
        _z_endpoint_clear(endpoint);
    }
    return ret;
}

static inline bool _z_unicast_link_read(_z_unicast_link_t *link, uint8_t *ptr, size_t *len) {
    _ZP_VARIANT_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_read(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_read(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_read(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_read(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_read(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_read(_ , ptr, len))
        (none, return false)
    );
    return false;
}

static inline bool _z_unicast_link_write(_z_unicast_link_t *link, const uint8_t *ptr, size_t *len) {
    _ZP_VARIANT_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_write(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_write(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_write(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_write(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_write(_ , ptr, len))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_write(_ , ptr, len))
        (none, return false)
    );
    return false;
}

static inline bool _z_unicast_link_is_reliable(const _z_unicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_is_reliable(_))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_is_reliable(_))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_is_reliable(_))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_is_reliable(_))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_is_reliable(_))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_is_reliable(_))
        (none, return false)
    );
    return false;
}

static inline uint16_t _z_unicast_link_get_mtu(const _z_unicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_get_mtu(_))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_get_mtu(_))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_get_mtu(_))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_get_mtu(_))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_get_mtu(_))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_get_mtu(_))
        (none, return 0)
    );
    return 0;
}

static inline bool _z_unicast_link_is_streamed(const _z_unicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_is_streamed(_))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_is_streamed(_))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_is_streamed(_))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_is_streamed(_))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_is_streamed(_))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_is_streamed(_))
        (none, return false)
    );
    return false;
}

static inline z_result_t _z_unicast_link_get_endpoints(const _z_unicast_link_t *link, char *local, size_t local_len,
                                                       char *remote, size_t remote_len) {
    _ZP_VARIANT_CONST_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_get_endpoints(_, local, local_len, remote, remote_len))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_get_endpoints(_, local, local_len, remote, remote_len))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_get_endpoints(_, local, local_len, remote, remote_len))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_get_endpoints(_, local, local_len, remote, remote_len))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_get_endpoints(_, local, local_len, remote, remote_len))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_get_endpoints(_, local, local_len, remote, remote_len))
        (none, return _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN)
    );
    return _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
}

static inline void _z_unicast_link_clear(_z_unicast_link_t *link) {
    _ZP_VARIANT_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(_z_unicast_link_tcp_clear(_))
        _ZP_UNICAST_LINK_CASE_UDP(_z_unicast_link_udp_clear(_))
        _ZP_UNICAST_LINK_CASE_BT(_z_unicast_link_bt_clear(_))
        _ZP_UNICAST_LINK_CASE_SERIAL(_z_unicast_link_serial_clear(_))
        _ZP_UNICAST_LINK_CASE_WS(_z_unicast_link_ws_clear(_))
        _ZP_UNICAST_LINK_CASE_TLS(_z_unicast_link_tls_clear(_))
        (none, )
    );
}

static inline const char *_z_unicast_link_get_protocol_name(const _z_unicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return TCP_SCHEMA)
        _ZP_UNICAST_LINK_CASE_UDP(return UDP_SCHEMA)
        _ZP_UNICAST_LINK_CASE_BT(return BT_SCHEMA)
        _ZP_UNICAST_LINK_CASE_SERIAL(return SERIAL_SCHEMA)
        _ZP_UNICAST_LINK_CASE_WS(return WS_SCHEMA)
        _ZP_UNICAST_LINK_CASE_TLS(return TLS_SCHEMA)
        (none, return "")
    );
    return "";
}

static inline _z_sys_net_socket_t *_z_unicast_link_get_socket(_z_unicast_link_t *link) {
    _ZP_VARIANT_VISIT(_z_unicast_link_inner, &link->_inner,
        _ZP_UNICAST_LINK_CASE_TCP(return _z_unicast_link_tcp_get_sock(_))
        _ZP_UNICAST_LINK_CASE_UDP(return _z_unicast_link_udp_get_sock(_))
        _ZP_UNICAST_LINK_CASE_BT(return _z_unicast_link_bt_get_sock(_))
        _ZP_UNICAST_LINK_CASE_SERIAL(return _z_unicast_link_serial_get_sock(_))
        _ZP_UNICAST_LINK_CASE_WS(return _z_unicast_link_ws_get_sock(_))
        _ZP_UNICAST_LINK_CASE_TLS(return _z_unicast_link_tls_get_sock(_))
        (none, return NULL)
    );
    return NULL;
}

z_result_t _z_unicast_link_get_locators(const _z_unicast_link_t *link, char *local, size_t local_len, char *remote,
                                        size_t remote_len);

bool _z_unicast_link_recv_zbuf(_z_unicast_link_t *link, _z_zbuf_t *zbf);
bool _z_unicast_link_send_wbuf(_z_unicast_link_t *link, const _z_wbuf_t *wbf);

#if Z_FEATURE_UNICAST_PEER == 1
#define _ZP_VARIANT_TEMPLATE_NAME _z_unicast_listener_inner
#if Z_FEATURE_LINK_TCP == 1
#define _ZP_VARIANT_TEMPLATE_1_TYPE _z_unicast_listener_tcp_t
#define _ZP_VARIANT_TEMPLATE_1_NAME tcp
#define _ZP_VARIANT_TEMPLATE_1_DESTROY_FN _z_unicast_listener_tcp_clear
#define _ZP_UNICAST_LISTENER_CASE_TCP(f) (tcp, f),
#else
#define _ZP_UNICAST_LISTENER_CASE_TCP(f)
#endif
#if Z_FEATURE_LINK_TLS == 1
#define _ZP_VARIANT_TEMPLATE_2_TYPE _z_unicast_listener_tls_t
#define _ZP_VARIANT_TEMPLATE_2_NAME tls
#define _ZP_VARIANT_TEMPLATE_2_DESTROY_FN _z_unicast_listener_tls_clear
#define _ZP_UNICAST_LISTENER_CASE_TLS(f) (tls, f),
#else
#define _ZP_UNICAST_LISTENER_CASE_TLS(f)
#endif
#include "zenoh-pico/collections/variant_template.h"

typedef struct _z_unicast_listener_t {
    _z_unicast_listener_inner_t _inner;
} _z_unicast_listener_t;

static inline _z_unicast_listener_t _z_unicast_listener_null(void) {
    _z_unicast_listener_t listener;
    listener._inner = _z_unicast_listener_inner_none();
    return listener;
}

// endpoint is not consumed upon failure
static inline z_result_t _z_unicast_listener_create(_z_unicast_listener_t *listener, _z_endpoint_t *endpoint,
                                                    const _z_config_t *config) {
    _ZP_UNUSED(config);
    listener->_inner = _z_unicast_listener_inner_none();
    z_result_t ret = _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
    if (false) {
#if Z_FEATURE_LINK_TCP == 1
    } else if (_z_endpoint_tcp_valid(endpoint) == Z_OK) {
        listener->_inner._tag = _z_unicast_listener_inner_tag_tcp;
        ret = _z_unicast_listener_tcp_create(&listener->_inner._tcp, endpoint);
#endif
#if Z_FEATURE_LINK_TLS == 1
    } else if (_z_endpoint_tls_valid(endpoint) == Z_OK) {
        endpoint->_config = _z_tls_merge_config(&endpoint->_config, config);
        listener->_inner._tag = _z_unicast_listener_inner_tag_tls;
        ret = _z_unicast_listener_tls_create(&listener->_inner._tls, endpoint);
#endif
    }
    if (ret == Z_OK) {
        _z_endpoint_clear(endpoint);
    } else {
        listener->_inner._tag = _z_unicast_listener_inner_tag_none;
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN);
    }
    return ret;
}

static inline z_result_t _z_unicast_listener_accept(_z_unicast_listener_t *listener, _z_unicast_link_t *link) {
    link->_inner = _z_unicast_link_inner_none();
    _ZP_VARIANT_VISIT(_z_unicast_listener_inner, &listener->_inner,
        _ZP_UNICAST_LISTENER_CASE_TCP({
            link->_inner._tag = _z_unicast_link_inner_tag_tcp;
            _Z_CLEAN_RETURN_IF_ERR(_z_unicast_listener_tcp_accept(_, &link->_inner._tcp), link->_inner._tag = _z_unicast_link_inner_tag_none);
            return _Z_RES_OK;
        })
        _ZP_UNICAST_LISTENER_CASE_TLS({
            link->_inner._tag = _z_unicast_link_inner_tag_tls;
            _Z_CLEAN_RETURN_IF_ERR(_z_unicast_listener_tls_accept(_, &link->_inner._tls), link->_inner._tag = _z_unicast_link_inner_tag_none);
            return _Z_RES_OK;
        })
        (none, return _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN)
    );
    return _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
}

static inline void _z_unicast_listener_clear(_z_unicast_listener_t *listener) {
    _ZP_VARIANT_VISIT(_z_unicast_listener_inner, &listener->_inner,
        _ZP_UNICAST_LISTENER_CASE_TCP(_z_unicast_listener_tcp_clear(_))
        _ZP_UNICAST_LISTENER_CASE_TLS(_z_unicast_listener_tls_clear(_))
        (none, )
    );
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* Z_FEATURE_UNICAST_TRANSPORT == 1 */

#endif /* ZENOH_PICO_UNICAST_LINK_H */
