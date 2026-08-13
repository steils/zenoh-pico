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

#ifndef ZENOH_PICO_LINK_CONFIG_RAWETH_H
#define ZENOH_PICO_LINK_CONFIG_RAWETH_H

#include "zenoh-pico/collections/intmap.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RAWETH_CFG_TUPLE_SEPARATOR '#'
#define RAWETH_CFG_LIST_SEPARATOR ","

#define RAWETH_CONFIG_ARGC 4

#define RAWETH_CONFIG_IFACE_KEY 0x01
#define RAWETH_CONFIG_IFACE_STR "iface"

#define RAWETH_CONFIG_ETHTYPE_KEY 0x02
#define RAWETH_CONFIG_ETHTYPE_STR "ethtype"

#define RAWETH_CONFIG_MAPPING_KEY 0x03
#define RAWETH_CONFIG_MAPPING_STR "mapping"

#define RAWETH_CONFIG_WHITELIST_KEY 0x04
#define RAWETH_CONFIG_WHITELIST_STR "whitelist"

// Ethtype must be at least 0x600 in network order
#define RAWETH_ETHTYPE_MIN_VALUE 0x600U

#define RAWETH_CONFIG_MAPPING_BUILD               \
    _z_str_intmapping_t args[RAWETH_CONFIG_ARGC]; \
    args[0]._key = RAWETH_CONFIG_IFACE_KEY;       \
    args[0]._str = RAWETH_CONFIG_IFACE_STR;       \
    args[1]._key = RAWETH_CONFIG_ETHTYPE_KEY;     \
    args[1]._str = RAWETH_CONFIG_ETHTYPE_STR;     \
    args[2]._key = RAWETH_CONFIG_MAPPING_KEY;     \
    args[2]._str = RAWETH_CONFIG_MAPPING_STR;     \
    args[3]._key = RAWETH_CONFIG_WHITELIST_KEY;   \
    args[3]._str = RAWETH_CONFIG_WHITELIST_STR;

size_t _z_raweth_config_strlen(const _z_str_intmap_t *s);
char *_z_raweth_config_to_str(const _z_str_intmap_t *s);
z_result_t _z_raweth_config_from_strn(_z_str_intmap_t *strint, const char *s, size_t n);
z_result_t _z_raweth_config_from_str(_z_str_intmap_t *strint, const char *s);

#ifdef __cplusplus
}
#endif

#endif /* ZENOH_PICO_LINK_CONFIG_RAWETH_H */
