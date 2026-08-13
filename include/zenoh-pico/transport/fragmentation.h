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
//

#ifndef ZENOH_PICO_TRANSPORT_FRAGMENTATION_H
#define ZENOH_PICO_TRANSPORT_FRAGMENTATION_H

#include "zenoh-pico/api/constants.h"
#include "zenoh-pico/config.h"
#include "zenoh-pico/protocol/iobuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#if Z_FEATURE_FRAGMENTATION == 1

typedef struct _z_dbuf_t {
    _z_wbuf_t best_effort;
    _z_wbuf_t reliable;
} _z_dbuf_t;

static inline z_result_t _z_dbuf_init(_z_dbuf_t *dbuf) {
    memset(dbuf, 0, sizeof(_z_dbuf_t));
    _Z_RETURN_IF_ERR(_z_wbuf_init(&dbuf->best_effort, Z_FRAG_MAX_SIZE, false));
    _Z_CLEAN_RETURN_IF_ERR(_z_wbuf_init(&dbuf->reliable, Z_FRAG_MAX_SIZE, false), _z_wbuf_clear(&dbuf->best_effort));
    return _Z_RES_OK;
}

static inline void _z_dbuf_clear(_z_dbuf_t *dbuf) {
    _z_wbuf_clear(&dbuf->best_effort);
    _z_wbuf_clear(&dbuf->reliable);
}

static inline _z_wbuf_t *_z_dbuf_get(_z_dbuf_t *dbuf, z_reliability_t reliability) {
    return reliability == Z_RELIABILITY_RELIABLE ? &dbuf->reliable : &dbuf->best_effort;
}

void _z_dbuf_reset(_z_dbuf_t *dbuf, z_reliability_t reliability);
void _z_dbuf_overflow(_z_dbuf_t *dbuf, z_reliability_t reliability);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_TRANSPORT_FRAGMENTATION_H */
