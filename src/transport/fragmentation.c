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
#include "zenoh-pico/transport/fragmentation.h"

#if Z_FEATURE_FRAGMENTATION == 1
void _z_dbuf_reset(_z_dbuf_t *dbuf, z_reliability_t reliability) {
    if (reliability == Z_RELIABILITY_RELIABLE) {
        _z_wbuf_reset(&dbuf->reliable);
    } else {
        _z_wbuf_reset(&dbuf->best_effort);
    }
}

void _z_dbuf_overflow(_z_dbuf_t *dbuf, z_reliability_t reliability) {
    if (reliability == Z_RELIABILITY_RELIABLE) {
        _z_wbuf_set_wpos(&dbuf->reliable, _z_wbuf_capacity(&dbuf->reliable));
    } else {
        _z_wbuf_set_wpos(&dbuf->best_effort, _z_wbuf_capacity(&dbuf->best_effort));
    }
}
#endif
