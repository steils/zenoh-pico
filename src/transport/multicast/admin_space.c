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

#include "zenoh-pico/transport/multicast/admin_space.h"

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/transport/multicast/transport.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/utils/json_encoder.h"

#if Z_FEATURE_MULTICAST_TRANSPORT == 1
#if Z_FEATURE_ADMIN_SPACE == 1
z_result_t _z_multicast_transport_manager_encode_locators_json(const _z_multicast_transport_manager_t *manager,
                                                               _z_json_encoder_t *je) {
    const _z_multicast_transport_group_t *group = NULL;
    _ZP_CONST_FOREACH (_z_multicast_transport_group_vec, &manager->_groups, group) {
        const char *locator =
            *_z_config_listen_vec_at(&manager->_parent->_session->_config._listen, (size_t)group->_locator_id);
        _Z_RETURN_IF_ERR(_z_json_encoder_write_string(je, locator));
    }
    return _Z_RES_OK;
}

z_result_t _z_multicast_transport_manager_encode_transports_json(const _z_multicast_transport_manager_t *manager,
                                                                 _z_json_encoder_t *je) {
    const _z_address_to_multicast_transport_peer_hmap_elem_t *entry = NULL;
    _ZP_CONST_FOREACH (_z_address_to_multicast_transport_peer_hmap, &manager->_peers, entry) {
        _Z_RETURN_IF_ERR(_z_json_encoder_start_object(je));

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "peer"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_zid(je, &entry->val._remote_zid));

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "whatami"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_whatami(je, entry->val._remote_whatami));

        _z_multicast_transport_peer_src_dst_address_t address;
        _z_multicast_transport_peer_src_dst_address_get(&entry->val, manager, &entry->key, &address);
        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "group"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_z_string(je, _z_string_view_deref(&address.src)));

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "links"));
        _Z_RETURN_IF_ERR(_z_json_encoder_start_array(je));
        _Z_RETURN_IF_ERR(_z_json_encoder_start_object(je));

        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "src"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_z_string(je, _z_string_view_deref(&address.src)));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_key(je, "dst"));
        _Z_RETURN_IF_ERR(_z_json_encoder_write_z_string(je, _z_string_view_deref(&address.dst)));

        _Z_RETURN_IF_ERR(_z_json_encoder_end_object(je));
        _Z_RETURN_IF_ERR(_z_json_encoder_end_array(je));

        _Z_RETURN_IF_ERR(_z_json_encoder_end_object(je));
    }
    return Z_OK;
}
#endif
#endif
