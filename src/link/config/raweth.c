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

#include "zenoh-pico/link/config/raweth.h"

#include <string.h>

size_t _z_raweth_config_strlen(const _z_str_intmap_t *s) {
    RAWETH_CONFIG_MAPPING_BUILD
    return _z_str_intmap_strlen(s, RAWETH_CONFIG_ARGC, args);
}
char *_z_raweth_config_to_str(const _z_str_intmap_t *s) {
    RAWETH_CONFIG_MAPPING_BUILD
    return _z_str_intmap_to_str(s, RAWETH_CONFIG_ARGC, args);
}

z_result_t _z_raweth_config_from_strn(_z_str_intmap_t *strint, const char *s, size_t n) {
    RAWETH_CONFIG_MAPPING_BUILD
    return _z_str_intmap_from_strn(strint, s, RAWETH_CONFIG_ARGC, args, n);
}

z_result_t _z_raweth_config_from_str(_z_str_intmap_t *strint, const char *s) {
    // Flawfinder: ignore [CWE-126] - public from_str() expects a conventional '\0'-terminated C string.
    return _z_raweth_config_from_strn(strint, s, strlen(s));
}
