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

#ifndef ZENOH_PICO_MULTICAST_LINK_H
#define ZENOH_PICO_MULTICAST_LINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "zenoh-pico/config.h"
#include "zenoh-pico/link/address.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/utils/config.h"
#include "zenoh-pico/utils/logging.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1

#if Z_FEATURE_LINK_UDP_MULTICAST == 1
#include "zenoh-pico/link/transport/udp_multicast.h"
#endif

#if Z_FEATURE_LINK_BLUETOOTH == 1
#include "zenoh-pico/link/transport/bt.h"
#endif

#if Z_FEATURE_RAWETH_TRANSPORT == 1
#include "zenoh-pico/link/transport/raweth.h"
#endif

#define _ZP_VARIANT_TEMPLATE_NAME _z_multicast_link_inner
#if Z_FEATURE_LINK_UDP_MULTICAST == 1
#define _ZP_VARIANT_TEMPLATE_1_TYPE _z_multicast_link_udp_t
#define _ZP_VARIANT_TEMPLATE_1_NAME udp
#define _ZP_VARIANT_TEMPLATE_1_DESTROY_FN _z_multicast_link_udp_clear
#define _ZP_MULTICAST_LINK_CASE_UDP(f) (udp, f),
#else
#define _ZP_MULTICAST_LINK_CASE_UDP(f)
#endif
#if Z_FEATURE_LINK_BLUETOOTH == 1
#define _ZP_VARIANT_TEMPLATE_2_TYPE _z_multicast_link_bt_t
#define _ZP_VARIANT_TEMPLATE_2_NAME bt
#define _ZP_VARIANT_TEMPLATE_2_DESTROY_FN _z_multicast_link_bt_clear
#define _ZP_MULTICAST_LINK_CASE_BT(f) (bt, f),
#else
#define _ZP_MULTICAST_LINK_CASE_BT(f)
#endif
#if Z_FEATURE_RAWETH_TRANSPORT == 1
#define _ZP_VARIANT_TEMPLATE_3_TYPE _z_multicast_link_raweth_t
#define _ZP_VARIANT_TEMPLATE_3_NAME raweth
#define _ZP_VARIANT_TEMPLATE_3_DESTROY_FN _z_multicast_link_raweth_clear
#define _ZP_MULTICAST_LINK_CASE_RAWETH(f) (raweth, f),
#else
#define _ZP_MULTICAST_LINK_CASE_RAWETH(f)
#endif
#include "zenoh-pico/collections/variant_template.h"

typedef struct _z_multicast_link_t {
    _z_multicast_link_inner_t _inner;
} _z_multicast_link_t;

static inline _z_multicast_link_t _z_multicast_link_null(void) {
    _z_multicast_link_t link;
    link._inner = _z_multicast_link_inner_none();
    return link;
}

static inline z_result_t _z_multicast_link_create(_z_multicast_link_t *link, _z_endpoint_t *endpoint,
                                                  const _z_config_t *config) {
    _ZP_UNUSED(config);
    link->_inner = _z_multicast_link_inner_none();
    z_result_t ret = _Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN;
    if (false) {
#if Z_FEATURE_LINK_UDP_MULTICAST == 1
    } else if (_z_endpoint_udp_multicast_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_multicast_link_inner_tag_udp;
        ret = _z_multicast_link_udp_create(&link->_inner._udp, endpoint);
#endif
#if Z_FEATURE_LINK_BLUETOOTH == 1
    } else if (_z_endpoint_bt_multicast_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_multicast_link_inner_tag_bt;
        ret = _z_multicast_link_bt_create(&link->_inner._bt, endpoint);
#endif
#if Z_FEATURE_RAWETH_TRANSPORT == 1
    } else if (_z_endpoint_raweth_multicast_valid(endpoint) == Z_OK) {
        link->_inner._tag = _z_multicast_link_inner_tag_raweth;
        ret = _z_multicast_link_raweth_create(&link->_inner._raweth, endpoint);
#endif
    }
    if (ret == Z_OK) {
        _z_endpoint_clear(endpoint);
    } else {
        link->_inner._tag = _z_multicast_link_inner_tag_none;
        _Z_ERROR_LOG(_Z_ERR_CONFIG_LOCATOR_SCHEMA_UNKNOWN);
    }
    return ret;
}

static inline bool _z_multicast_link_read(_z_multicast_link_t *link, uint8_t *ptr, size_t *len,
                                          _z_link_address_t *addr_out) {
    _z_link_address_destroy(addr_out);  // Clear the address before reading new data
    _ZP_VARIANT_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return _z_multicast_link_udp_read(_, ptr, len, addr_out))
        _ZP_MULTICAST_LINK_CASE_BT(return _z_multicast_link_bt_read(_, ptr, len, addr_out))
        _ZP_MULTICAST_LINK_CASE_RAWETH(return _z_multicast_link_raweth_read(_, ptr, len, addr_out))
        (none, return false)
    );
    return false;
}

static inline bool _z_multicast_link_write(_z_multicast_link_t *link, const uint8_t *ptr, size_t *len) {
    _ZP_VARIANT_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return _z_multicast_link_udp_write(_, ptr, len))
        _ZP_MULTICAST_LINK_CASE_BT(return _z_multicast_link_bt_write(_, ptr, len))
        _ZP_MULTICAST_LINK_CASE_RAWETH(return _z_multicast_link_raweth_write(_, ptr, len))
        (none, return false)
    );
    return false;
}

static inline uint16_t _z_multicast_link_get_mtu(const _z_multicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return _z_multicast_link_udp_get_mtu(_))
        _ZP_MULTICAST_LINK_CASE_BT(return _z_multicast_link_bt_get_mtu(_))
        _ZP_MULTICAST_LINK_CASE_RAWETH(return _z_multicast_link_raweth_get_mtu(_))
        (none, return 0)
    );
    return 0;
}

static inline bool _z_multicast_link_is_reliable(const _z_multicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return _z_multicast_link_udp_is_reliable(_))
        _ZP_MULTICAST_LINK_CASE_BT(return _z_multicast_link_bt_is_reliable(_))
        _ZP_MULTICAST_LINK_CASE_RAWETH(return _z_multicast_link_raweth_is_reliable(_))
        (none, return false)
    );
    return false;
}

static inline bool _z_multicast_link_is_streamed(const _z_multicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return _z_multicast_link_udp_is_streamed(_))
        _ZP_MULTICAST_LINK_CASE_BT(return _z_multicast_link_bt_is_streamed(_))
        _ZP_MULTICAST_LINK_CASE_RAWETH(return _z_multicast_link_raweth_is_streamed(_))
        (none, return false)
    );
    return false;
}

static inline void _z_multicast_link_clear(_z_multicast_link_t *link) {
    _ZP_VARIANT_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(_z_multicast_link_udp_clear(_))
        _ZP_MULTICAST_LINK_CASE_BT(_z_multicast_link_bt_clear(_))
        _ZP_MULTICAST_LINK_CASE_RAWETH(_z_multicast_link_raweth_clear(_))
        (none, )
    );
}

static inline const char *_z_multicast_link_get_protocol_name(const _z_multicast_link_t *link) {
    _ZP_VARIANT_CONST_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return UDP_SCHEMA)
        _ZP_MULTICAST_LINK_CASE_BT(return BT_SCHEMA)
        _ZP_MULTICAST_LINK_CASE_RAWETH(return RAWETH_SCHEMA)
        (none, return "")
    );
    return "";
}

static inline _z_sys_net_socket_t *_z_multicast_link_get_socket(_z_multicast_link_t *link) {
    _ZP_VARIANT_VISIT(_z_multicast_link_inner, &link->_inner,
        _ZP_MULTICAST_LINK_CASE_UDP(return _z_multicast_link_udp_get_sock(_))
        _ZP_MULTICAST_LINK_CASE_BT(return _z_multicast_link_bt_get_sock(_))
        _ZP_MULTICAST_LINK_CASE_RAWETH(return _z_multicast_link_raweth_get_sock(_))
        (none, return NULL)
    );
    return NULL;
}

z_result_t _z_multicast_link_get_dst_locator(const _z_multicast_link_t *link, const uint8_t *address,
                                             size_t address_len, char *buf, size_t buf_len);
bool _z_multicast_link_send_wbuf(_z_multicast_link_t *link, const _z_wbuf_t *wbf);
bool _z_multicast_link_recv_zbuf(_z_multicast_link_t *link, _z_zbuf_t *zbf, _z_link_address_t *addr_out);
#ifdef __cplusplus
}
#endif

#endif /* Z_FEATURE_MULTICAST_TRANSPORT == 1 */
#endif /* ZENOH_PICO_MULTICAST_LINK_H */
