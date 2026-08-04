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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "zenoh-pico.h"
#include "zenoh-pico/link/endpoint.h"
#include "zenoh-pico/link/unicast_link.h"
#include "zenoh-pico/net/session.h"
#include "zenoh-pico/protocol/iobuf.h"
#include "zenoh-pico/runtime/background_executor.h"
#include "zenoh-pico/transport/transport.h"
#include "zenoh-pico/transport/unicast/rx.h"
#include "zenoh-pico/transport/unicast/transport.h"
#include "zenoh-pico/transport/unicast/tx.h"

#if Z_FEATURE_UNICAST_PEER == 1 && Z_FEATURE_LINK_TCP == 1

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            abort();                                                                         \
        }                                                                                    \
    } while (false)

static bool open_server(z_owned_session_t *server, char *locator, size_t locator_len) {
    for (unsigned int port = 17447; port < 17479; port++) {
        snprintf(locator, locator_len, "tcp/127.0.0.1:%u", port);
        z_owned_config_t config;
        z_config_default(&config);
        zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "peer");
        zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, locator);
        config._val._listen_exit_on_failure._parsed = true;
        if (z_open(server, z_move(config), NULL) == _Z_RES_OK) {
            return true;
        }
    }
    return false;
}

#if Z_FEATURE_MULTI_THREAD == 1

static size_t pending_count(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    CHECK(_z_background_executor_suspend(&inner->_runtime) == _Z_RES_OK);
    size_t count = _z_unicast_transport_manager_get_pending_count(&inner->_transport_manager._unicast);
    CHECK(_z_background_executor_resume(&inner->_runtime) == _Z_RES_OK);
    return count;
}

static size_t established_count(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    CHECK(_z_background_executor_suspend(&inner->_runtime) == _Z_RES_OK);
    size_t count = _z_unicast_transport_manager_get_peers_count(&inner->_transport_manager._unicast);
    CHECK(_z_background_executor_resume(&inner->_runtime) == _Z_RES_OK);
    return count;
}

static size_t active_slot_count(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    CHECK(_z_background_executor_suspend(&inner->_runtime) == _Z_RES_OK);
    size_t count = _z_unicast_transport_peer_hmap_size(&inner->_transport_manager._unicast._peers);
    CHECK(_z_background_executor_resume(&inner->_runtime) == _Z_RES_OK);
    return count;
}

static _z_id_t make_id(uint8_t value) {
    _z_id_t id = _z_id_empty();
    id.id[0] = value;
    return id;
}

static z_clock_t deadline_after(unsigned long timeout_ms) {
    z_clock_t deadline = z_clock_now();
    z_clock_advance_ms(&deadline, timeout_ms);
    return deadline;
}

static bool wait_for_count(size_t (*count_f)(z_owned_session_t *), z_owned_session_t *session, size_t expected,
                           unsigned long timeout_ms) {
    z_clock_t start = z_clock_now();
    while (z_clock_elapsed_ms(&start) < timeout_ms) {
        if (count_f(session) == expected) {
            return true;
        }
        z_sleep_ms(10);
    }
    return count_f(session) == expected;
}

static _z_unicast_link_t open_silent_link(z_owned_session_t *server, const char *locator) {
    _z_string_view_t locator_view = _z_string_view_make_from_str(locator);
    _z_endpoint_t endpoint;
    CHECK(_z_endpoint_from_string(&endpoint, _z_string_view_deref(&locator_view)) == _Z_RES_OK);
    _z_unicast_link_t link = _z_unicast_link_null();
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(server);
    CHECK(_z_unicast_link_create(&link, &endpoint, &inner->_config) == _Z_RES_OK);
    return link;
}

static _z_transport_message_t recv_transport_message(_z_unicast_link_t *link, _z_zbuf_t *rx) {
    _z_transport_message_t message = {0};
    CHECK(_z_unicast_link_recv_t_msg(link, &message, rx, deadline_after(1000)) == _Z_RES_OK);
    return message;
}

static void close_all_peers(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    CHECK(_z_background_executor_suspend(&inner->_runtime) == _Z_RES_OK);
    _z_unicast_transport_manager_t *manager = &inner->_transport_manager._unicast;
    while (_z_unicast_transport_manager_get_peers_count(manager) > 0) {
        _z_unicast_peer_slot_id_t peer_id = _z_unicast_transport_peer_established_begin(manager);
        CHECK(_z_unicast_transport_manager_close_peer(manager, peer_id, NULL, NULL) == _Z_RES_OK);
    }
    CHECK(_z_background_executor_resume(&inner->_runtime) == _Z_RES_OK);
}

#if Z_MAX_NUM_UNICAST_PEERS >= 2
static void test_two_socket_handshakes_progress(z_owned_session_t *server, const char *locator) {
    _z_unicast_link_t first = open_silent_link(server, locator);
    _z_unicast_link_t second = open_silent_link(server, locator);
    CHECK(wait_for_count(pending_count, server, 2, 1000));
    CHECK(active_slot_count(server) == 2);

    _z_transport_message_t first_init =
        _z_t_msg_make_init_syn(Z_WHATAMI_CLIENT, make_id(31), Z_BATCH_UNICAST_SIZE);
    _z_transport_message_t second_init =
        _z_t_msg_make_init_syn(Z_WHATAMI_CLIENT, make_id(32), Z_BATCH_UNICAST_SIZE);
    CHECK(_z_unicast_link_send_t_msg(&first, &first_init) == _Z_RES_OK);
    CHECK(_z_unicast_link_send_t_msg(&second, &second_init) == _Z_RES_OK);

    _z_zbuf_t first_rx;
    _z_zbuf_t second_rx;
    CHECK(_z_zbuf_init(&first_rx, Z_BATCH_UNICAST_SIZE) == _Z_RES_OK);
    CHECK(_z_zbuf_init(&second_rx, Z_BATCH_UNICAST_SIZE) == _Z_RES_OK);
    _z_transport_message_t first_init_ack = recv_transport_message(&first, &first_rx);
    _z_transport_message_t second_init_ack = recv_transport_message(&second, &second_rx);
    CHECK(_Z_MID(first_init_ack._header) == _Z_MID_T_INIT);
    CHECK(_Z_HAS_FLAG(first_init_ack._header, _Z_FLAG_T_INIT_A));
    CHECK(_Z_MID(second_init_ack._header) == _Z_MID_T_INIT);
    CHECK(_Z_HAS_FLAG(second_init_ack._header, _Z_FLAG_T_INIT_A));

    _z_transport_message_t first_open =
        _z_t_msg_make_open_syn(1000, 101, _z_slice_view_deref(&first_init_ack._body._init._cookie));
    _z_transport_message_t second_open =
        _z_t_msg_make_open_syn(1000, 102, _z_slice_view_deref(&second_init_ack._body._init._cookie));
    CHECK(_z_unicast_link_send_t_msg(&first, &first_open) == _Z_RES_OK);
    CHECK(_z_unicast_link_send_t_msg(&second, &second_open) == _Z_RES_OK);

    _z_zbuf_reset(&first_rx);
    _z_zbuf_reset(&second_rx);
    _z_transport_message_t first_open_ack = recv_transport_message(&first, &first_rx);
    _z_transport_message_t second_open_ack = recv_transport_message(&second, &second_rx);
    CHECK(_Z_MID(first_open_ack._header) == _Z_MID_T_OPEN);
    CHECK(_Z_HAS_FLAG(first_open_ack._header, _Z_FLAG_T_OPEN_A));
    CHECK(_Z_MID(second_open_ack._header) == _Z_MID_T_OPEN);
    CHECK(_Z_HAS_FLAG(second_open_ack._header, _Z_FLAG_T_OPEN_A));
    CHECK(wait_for_count(established_count, server, 2, 1000));
    CHECK(pending_count(server) == 0);
    CHECK(active_slot_count(server) == 2);

    close_all_peers(server);
    _z_zbuf_clear(&first_rx);
    _z_zbuf_clear(&second_rx);
    _z_unicast_link_clear(&first);
    _z_unicast_link_clear(&second);
}

#endif

static void test_malformed_handshake_is_removed(z_owned_session_t *server, const char *locator) {
    _z_unicast_link_t link = open_silent_link(server, locator);
    CHECK(wait_for_count(pending_count, server, 1, 1000));

    _z_slice_t empty_cookie = _z_slice_null();
    _z_transport_message_t open_syn = _z_t_msg_make_open_syn(1000, 1, &empty_cookie);
    CHECK(_z_unicast_link_send_t_msg(&link, &open_syn) == _Z_RES_OK);
    CHECK(wait_for_count(pending_count, server, 0, 1000));
    _z_unicast_link_clear(&link);
}

static z_owned_session_t open_client(const char *locator) {
    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "client");
    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, locator);
    config._val._connect_exit_on_failure._parsed = true;
    z_owned_session_t client;
    CHECK(z_open(&client, z_move(config), NULL) == _Z_RES_OK);
    return client;
}

static void close_first_peer_and_keep_executor_suspended(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    CHECK(_z_background_executor_suspend(&inner->_runtime) == _Z_RES_OK);
    _z_unicast_transport_manager_t *manager = &inner->_transport_manager._unicast;
    _z_unicast_peer_slot_id_t peer_id = _z_unicast_transport_peer_established_begin(manager);
    CHECK(peer_id != _z_unicast_transport_peer_hmap_end(&manager->_peers));
    _z_close_reason_t reason = _Z_CLOSE_REASON_GENERIC;
    CHECK(_z_unicast_transport_manager_close_peer(manager, peer_id, &reason, NULL) == _Z_RES_OK);
}

int main(void) {
    char locator[64];
    z_owned_session_t server;
    CHECK(open_server(&server, locator, sizeof(locator)));

#if Z_MAX_NUM_UNICAST_PEERS >= 2
    test_two_socket_handshakes_progress(&server, locator);
#endif
    test_malformed_handshake_is_removed(&server, locator);

    _z_unicast_link_t silent = open_silent_link(&server, locator);
    CHECK(wait_for_count(pending_count, &server, 1, 1000));

    z_owned_session_t client = open_client(locator);
    CHECK(wait_for_count(established_count, &server, 1, 1000));
    CHECK(active_slot_count(&server) == 2);
    CHECK(wait_for_count(pending_count, &server, 0, Z_TRANSPORT_ACCEPT_TIMEOUT + 500));
    CHECK(active_slot_count(&server) == 1);

    close_first_peer_and_keep_executor_suspended(&server);
    CHECK(wait_for_count(pending_count, &client, 1, 1500));
    z_sleep_ms(2 * Z_CONFIG_SOCKET_TIMEOUT);
    CHECK(pending_count(&client) == 1);
    CHECK(established_count(&client) == 0);
    CHECK(_z_background_executor_resume(&_Z_OWNED_RC_IN_VAL(&server)->_runtime) == _Z_RES_OK);
    CHECK(wait_for_count(established_count, &server, 1, 1500));
    CHECK(wait_for_count(established_count, &client, 1, 1500));

    _z_unicast_link_clear(&silent);

    z_drop(z_move(client));
    z_drop(z_move(server));
    return 0;
}

#else

static size_t pending_count(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    return _z_unicast_transport_manager_get_pending_count(&inner->_transport_manager._unicast);
}

static size_t established_count(z_owned_session_t *session) {
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(session);
    return _z_unicast_transport_manager_get_peers_count(&inner->_transport_manager._unicast);
}

static z_owned_session_t open_peer(void) {
    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "peer");
    z_owned_session_t peer;
    CHECK(z_open(&peer, z_move(config), NULL) == _Z_RES_OK);
    return peer;
}

static void start_async_connect(z_owned_session_t *client, const char *locator) {
    _z_string_view_t locator_view = _z_string_view_make_from_str(locator);
    _z_session_t *inner = _Z_OWNED_RC_IN_VAL(client);
    CHECK(!_z_fut_handle_is_null(inner->_transport_manager._read_task));
    CHECK(_z_transport_manager_connect_peer(&inner->_transport_manager, _z_string_view_deref(&locator_view), 0) ==
          _Z_RES_OK);
}

static bool spin_until_established(z_owned_session_t *server, z_owned_session_t *client, unsigned long timeout_ms) {
    z_clock_t start = z_clock_now();
    while (z_clock_elapsed_ms(&start) < timeout_ms) {
        (void)zp_spin_once(z_loan(*server));
        (void)zp_spin_once(z_loan(*client));
        if (established_count(server) == 1 && established_count(client) == 1) {
            return true;
        }
        z_sleep_ms(1);
    }
    return false;
}

int main(void) {
    char locator[64];
    z_owned_session_t server;
    CHECK(open_server(&server, locator, sizeof(locator)));
    z_owned_session_t client = open_peer();

    start_async_connect(&client, locator);
    CHECK(pending_count(&client) == 1);
    CHECK(spin_until_established(&server, &client, Z_TRANSPORT_CONNECT_TIMEOUT + 1000));
    CHECK(pending_count(&server) == 0);
    CHECK(pending_count(&client) == 0);

    z_drop(z_move(client));
    z_drop(z_move(server));
    return 0;
}

#endif

#else

int main(void) { return 0; }

#endif
