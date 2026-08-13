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

#include "zenoh-pico/link/address.h"

#include "zenoh-pico/utils/hash.h"

bool _z_link_address_eq(const _z_link_address_t *left, const _z_link_address_t *right) {
    if (_z_link_address_size(left) != _z_link_address_size(right)) {
        return false;
    }
    return memcmp(_z_link_address_const_data(left), _z_link_address_const_data(right), _z_link_address_size(left)) == 0;
}

size_t _z_link_address_hash(const _z_link_address_t *address) {
    return _z_fnv1_hash(_z_link_address_const_data(address), _z_link_address_size(address));
}
