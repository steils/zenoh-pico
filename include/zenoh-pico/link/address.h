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

#ifndef ZENOH_PICO_LINK_ADDRESS_H
#define ZENOH_PICO_LINK_ADDRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// define _z_link_address_t as a static vector of uint8_t with a maximum capacity of 32 bytes
#define _ZP_STATIC_VECTOR_TEMPLATE_NAME _z_link_address
#define _ZP_STATIC_VECTOR_TEMPLATE_ELEM_TYPE uint8_t
#define _ZP_STATIC_VECTOR_TEMPLATE_CAPACITY 32
#include "zenoh-pico/collections/static_vector_template.h"

bool _z_link_address_eq(const _z_link_address_t *left, const _z_link_address_t *right);
size_t _z_link_address_hash(const _z_link_address_t *address);

#ifdef __cplusplus
}
#endif
#endif /* ZENOH_PICO_LINK_ADDRESS_H */
