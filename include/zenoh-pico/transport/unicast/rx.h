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

#ifndef ZENOH_PICO_UNICAST_TRANSPORT_RX_H
#define ZENOH_PICO_UNICAST_TRANSPORT_RX_H

#include <stddef.h>

#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/protocol/definitions/transport.h"
#include "zenoh-pico/system/platform.h"
#include "zenoh-pico/transport/rx.h"
#include "zenoh-pico/transport/unicast/transport.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_UNICAST_TRANSPORT == 1
z_result_t _z_unicast_link_recv_t_msg(_z_unicast_link_t *link, _z_transport_message_t *t_msg, _z_zbuf_t *zbf,
                                      z_clock_t recv_deadline);

z_result_t _z_unicast_transport_manager_read(_z_unicast_transport_manager_t *manager,
                                             const _z_ready_links_mask_bitset_t *ready_links);
#endif
#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_UNICAST_TRANSPORT_RX_H */
