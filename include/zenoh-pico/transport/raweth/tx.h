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

#ifndef ZENOH_PICO_RAWETH_TX_H
#define ZENOH_PICO_RAWETH_TX_H

#include "zenoh-pico/link/multicast_link.h"
#include "zenoh-pico/protocol/definitions/network.h"
#include "zenoh-pico/protocol/definitions/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_MULTICAST_TRANSPORT == 1 && Z_FEATURE_RAWETH_TRANSPORT == 1
void _z_raweth_wbf_and_link_prepare(_z_wbuf_t *wbf, _z_multicast_link_raweth_t *zl, const _z_network_message_t *n_msg);
void _z_raweth_wbuf_and_link_finalize(_z_wbuf_t *wbf, _z_multicast_link_raweth_t *zl);
#endif /* Z_FEATURE_MULTICAST_TRANSPORT == 1 && Z_FEATURE_RAWETH_TRANSPORT == 1 */
#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_RAWETH_TX_H */
