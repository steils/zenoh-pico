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

#if defined(ZP_PLATFORM_SOCKET_OPENCR) && (Z_FEATURE_LINK_UDP_MULTICAST == 1)

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "zenoh-pico/link/transport/udp_multicast.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/pointers.h"

z_result_t _z_open_udp_multicast(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, _z_sys_net_endpoint_t *lep,
                                 uint32_t tout, const char *iface) {
    z_result_t ret = _Z_RES_OK;
    (void)(rep);
    (void)(tout);
    (void)(iface);

    sock->_udp = new WiFiUDP();
    if (sock->_udp == NULL) {
        return _Z_ERR_GENERIC;
    }
    if (!sock->_udp->begin(55555)) {
        _Z_ERROR_LOG(_Z_ERR_GENERIC);
        ret = _Z_ERR_GENERIC;
    }

    lep->_iptcp._addr = new IPAddress();

    if (ret != _Z_RES_OK) {
        delete sock->_udp;
        sock->_udp = NULL;
        delete lep->_iptcp._addr;
        lep->_iptcp._addr = NULL;
    }

    return ret;
}

z_result_t _z_listen_udp_multicast(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout,
                                   const char *iface, const char *join) {
    (void)tout;
    (void)iface;
    (void)join;
    z_result_t ret = _Z_RES_OK;

    sock->_udp = new WiFiUDP();
    if (!sock->_udp->beginMulticast(*rep._iptcp._addr, rep._iptcp._port)) {
        _Z_ERROR_LOG(_Z_ERR_GENERIC);
        ret = _Z_ERR_GENERIC;
    }

    if (ret != _Z_RES_OK) {
        delete sock->_udp;
        sock->_udp = NULL;
    }

    return ret;
}

void _z_close_udp_multicast(_z_sys_net_socket_t *sockrecv, _z_sys_net_socket_t *socksend,
                            const _z_sys_net_endpoint_t rep, const _z_sys_net_endpoint_t lep) {
    _ZP_UNUSED(rep);
    _ZP_UNUSED(lep);

    if (sockrecv->_udp != NULL) {
        sockrecv->_udp->stop();
        delete sockrecv->_udp;
        sockrecv->_udp = NULL;
    }

    if (socksend->_udp != NULL) {
        socksend->_udp->stop();
        delete socksend->_udp;
        socksend->_udp = NULL;
    }
}

size_t _z_read_udp_multicast(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len, const _z_sys_net_endpoint_t lep,
                             _z_link_address_t *addr_out) {
    _ZP_UNUSED(lep);
    ssize_t rb = 0;
    do {
        rb = sock._udp->parsePacket();
    } while (rb == 0);

    if (rb <= (ssize_t)len) {
        // flawfinder: ignore
        if (sock._udp->read(ptr, rb) == rb) {
            IPAddress rip = sock._udp->remoteIP();
            uint16_t rport = sock._udp->remotePort();
            _z_link_address_push_back(addr_out, rip[0]);
            _z_link_address_push_back(addr_out, rip[1]);
            _z_link_address_push_back(addr_out, rip[2]);
            _z_link_address_push_back(addr_out, rip[3]);
            const uint8_t *port_bytes = (const uint8_t *)&rport;
            _z_link_address_push_back(addr_out, port_bytes[0]);
            _z_link_address_push_back(addr_out, port_bytes[1]);
        } else {
            rb = 0;
        }
    }

    return rb;
}

size_t _z_send_udp_multicast(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len,
                             const _z_sys_net_endpoint_t rep) {
    sock._udp->beginPacket(*rep._iptcp._addr, rep._iptcp._port);
    sock._udp->write(ptr, len);
    sock._udp->endPacket();

    return len;
}

z_result_t _z_udp_multicast_endpoint_init_from_address(_z_sys_net_endpoint_t *ep, const _z_string_t *address) {
    return _z_udp_multicast_default_endpoint_init_from_address(ep, address);
}

void _z_udp_multicast_endpoint_clear(_z_sys_net_endpoint_t *ep) { _z_udp_multicast_default_endpoint_clear(ep); }

z_result_t _z_udp_multicast_open(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, _z_sys_net_endpoint_t *lep,
                                 uint32_t tout, const char *iface) {
    return _z_open_udp_multicast(sock, rep, lep, tout, iface);
}

z_result_t _z_udp_multicast_listen(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout,
                                   const char *iface, const char *join) {
    return _z_listen_udp_multicast(sock, rep, tout, iface, join);
}

void _z_udp_multicast_close(_z_sys_net_socket_t *sockrecv, _z_sys_net_socket_t *socksend,
                            const _z_sys_net_endpoint_t rep, const _z_sys_net_endpoint_t lep) {
    _z_close_udp_multicast(sockrecv, socksend, rep, lep);
}

size_t _z_udp_multicast_read(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len, const _z_sys_net_endpoint_t lep,
                             _z_link_address_t *ep_out) {
    return _z_read_udp_multicast(sock, ptr, len, lep, ep_out);
}

size_t _z_udp_multicast_write(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len,
                              const _z_sys_net_endpoint_t rep) {
    return _z_send_udp_multicast(sock, ptr, len, rep);
}

}  // extern "C"

#endif
