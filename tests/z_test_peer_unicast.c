//
// Copyright (c) 2025 ZettaScale Technology
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico.h"

#if Z_FEATURE_SUBSCRIPTION == 1 && Z_FEATURE_PUBLICATION == 1 && Z_FEATURE_QUERY == 1 && Z_FEATURE_QUERYABLE == 1 && \
    Z_FEATURE_MULTI_THREAD == 1 && Z_FEATURE_UNICAST_PEER == 1
typedef struct _node_ctx {
    z_owned_config_t config;
    const char *keyexpr_out;
    int id;
    int sub_msg_nb;
    int qybl_msg_nb;
} _node_ctx_t;

const char *keyexpr_in = "test/**";
const char *qybl_val = "Queryable data";
const char *pub_val = "Publisher data";
const int tx_nb = 5;
const int rx_nb = 15;

void query_handler(z_loaned_query_t *query, void *ctx) {
    _node_ctx_t *node_ctx = (_node_ctx_t *)ctx;
    z_view_string_t keystr;
    z_keyexpr_as_view_string(z_query_keyexpr(query), &keystr);
    z_view_string_t params;
    z_query_parameters(query, &params);
    printf(" >> [Queryable %d] Received ('%.*s': '%.*s')\n", node_ctx->id, (int)z_string_len(z_loan(keystr)),
           z_string_data(z_loan(keystr)), (int)z_string_len(z_loan(params)), z_string_data(z_loan(params)));
    // Reply value
    z_owned_bytes_t reply_payload;
    z_bytes_from_static_str(&reply_payload, qybl_val);
    z_query_reply(query, z_query_keyexpr(query), z_move(reply_payload), NULL);
    node_ctx->qybl_msg_nb++;
}

void pub_handler(z_loaned_sample_t *sample, void *ctx) {
    _node_ctx_t *node_ctx = (_node_ctx_t *)ctx;
    z_view_string_t keystr;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &keystr);
    z_owned_string_t value;
    z_bytes_to_string(z_sample_payload(sample), &value);
    printf(">> [Subscriber %d] Received ('%.*s': '%.*s')\n", node_ctx->id, (int)z_string_len(z_loan(keystr)),
           z_string_data(z_loan(keystr)), (int)z_string_len(z_loan(value)), z_string_data(z_loan(value)));
    z_drop(z_move(value));
    node_ctx->sub_msg_nb++;
}

void *node_task(void *ptr) {
    _node_ctx_t *ctx = (_node_ctx_t *)ptr;

    // Open session
    printf("Opening session...\n");
    z_owned_session_t s;
    if (z_open(&s, z_move(ctx->config), NULL) != Z_OK) {
        printf("Unable to open session!\n");
        return NULL;
    }
    // Create keyexprs
    z_view_keyexpr_t sub_qybl_ke;
    if (z_view_keyexpr_from_str(&sub_qybl_ke, keyexpr_in) != Z_OK) {
        printf("%s is not a valid key expression\n", keyexpr_in);
        return NULL;
    }
    z_view_keyexpr_t pub_qry_ke;
    if (z_view_keyexpr_from_str(&pub_qry_ke, ctx->keyexpr_out) < 0) {
        printf("%s is not a valid key expression\n", ctx->keyexpr_out);
        return NULL;
    }
    // Declare subscriber
    printf("Declaring Subscriber on '%s'...\n", keyexpr_in);
    z_owned_closure_sample_t sub_cb;
    z_closure(&sub_cb, pub_handler, NULL, ctx);
    z_subscriber_options_t sub_opts;
    z_subscriber_options_default(&sub_opts);
#if Z_FEATURE_LOCAL_SUBSCRIBER == 1
    sub_opts.allowed_origin = Z_LOCALITY_REMOTE;
#endif
    if (z_declare_background_subscriber(z_loan(s), z_loan(sub_qybl_ke), z_move(sub_cb), &sub_opts) != Z_OK) {
        printf("Unable to declare subscriber.\n");
        return NULL;
    }
    // Declare queryable
    printf("Creating Queryable on '%s'...\n", keyexpr_in);
    z_owned_closure_query_t qybl_cb;
    z_closure(&qybl_cb, query_handler, NULL, ctx);
    z_queryable_options_t qybl_opts;
    z_queryable_options_default(&qybl_opts);
#if Z_FEATURE_LOCAL_QUERYABLE == 1
    qybl_opts.allowed_origin = Z_LOCALITY_REMOTE;
#endif
    if (z_declare_background_queryable(z_loan(s), z_loan(sub_qybl_ke), z_move(qybl_cb), &qybl_opts) != Z_OK) {
        printf("Unable to create queryable.\n");
        return NULL;
    }
    // Declare publisher
    printf("Declaring publisher for '%s'...\n", ctx->keyexpr_out);
    z_owned_publisher_t pub;
    z_publisher_options_t pub_opts;
    z_publisher_options_default(&pub_opts);
#if Z_FEATURE_LOCAL_SUBSCRIBER == 1
    pub_opts.allowed_destination = Z_LOCALITY_REMOTE;
#endif
    if (z_declare_publisher(z_loan(s), &pub, z_loan(pub_qry_ke), &pub_opts) != Z_OK) {
        printf("Unable to declare publisher for key expression!\n");
        return NULL;
    }
    // Declare querier
    printf("Declaring Querier on '%s'...\n", ctx->keyexpr_out);
    z_owned_querier_t querier;
    z_querier_options_t qry_opts;
    z_querier_options_default(&qry_opts);
#if Z_FEATURE_LOCAL_QUERYABLE == 1
    qry_opts.allowed_destination = Z_LOCALITY_REMOTE;
#endif
    if (z_declare_querier(z_loan(s), &querier, z_loan(pub_qry_ke), &qry_opts) != Z_OK) {
        printf("Unable to declare Querier for key expression!\n");
        return NULL;
    }
    // Wait for other nodes to come online
    z_sleep_s(5);
    printf("Starting sending data\n");
    // Publish data
    char buf[256];
    for (int idx = 0; idx < tx_nb; ++idx) {
        z_sleep_s(1);
        // Create payload
        sprintf(buf, "[%4d] %s", idx, pub_val);
        printf("[Publisher %d] Sending ('%s': '%s')...\n", ctx->id, ctx->keyexpr_out, buf);
        z_owned_bytes_t payload;
        z_bytes_copy_from_str(&payload, buf);
        // Send data
        z_publisher_put(z_loan(pub), z_move(payload), NULL);
    }
    // Querier data
    for (int idx = 0; idx < tx_nb; ++idx) {
        z_sleep_s(1);
        // Create payload
        sprintf(buf, "[%4d] %s", idx, "");
        printf("[Querier %d] Sending ('%s': '%s')...\n", ctx->id, ctx->keyexpr_out, buf);
        // Query data
        z_owned_fifo_handler_reply_t qry_handler;
        z_owned_closure_reply_t qry_closure;
        z_fifo_channel_reply_new(&qry_closure, &qry_handler, 16);
        z_querier_get(z_loan(querier), NULL, z_move(qry_closure), NULL);
        // Process received data
        z_owned_reply_t reply;
        for (z_result_t res = z_recv(z_loan(qry_handler), &reply); res == Z_OK;
             res = z_recv(z_loan(qry_handler), &reply)) {
            if (z_reply_is_ok(z_loan(reply))) {
                const z_loaned_sample_t *sample = z_reply_ok(z_loan(reply));
                z_view_string_t keystr;
                z_keyexpr_as_view_string(z_sample_keyexpr(sample), &keystr);
                z_owned_string_t replystr;
                z_bytes_to_string(z_sample_payload(sample), &replystr);
                printf(">> [Query %d] Received ('%.*s': '%.*s')\n", ctx->id, (int)z_string_len(z_loan(keystr)),
                       z_string_data(z_loan(keystr)), (int)z_string_len(z_loan(replystr)),
                       z_string_data(z_loan(replystr)));
                z_drop(z_move(replystr));
            }
            z_drop(z_move(reply));
        }
        z_drop(z_move(qry_handler));
    }
    // Wait for sub & queryable data
    z_sleep_s(5);
    printf("Node %d: Received %d subs, %d queries\n", ctx->id, ctx->sub_msg_nb, ctx->qybl_msg_nb);
    assert(ctx->sub_msg_nb >= rx_nb);
    assert(ctx->qybl_msg_nb >= rx_nb);
    // Clean up
    z_drop(z_move(pub));
    z_drop(z_move(querier));
    z_drop(z_move(s));
    return NULL;
}

static void test_packet_transmission(void) {
    // Create node context
    _node_ctx_t node_ctx_0 = {.keyexpr_out = "test/A", .id = 0, .qybl_msg_nb = 0, .sub_msg_nb = 0};
    _node_ctx_t node_ctx_1 = {.keyexpr_out = "test/B", .id = 1, .qybl_msg_nb = 0, .sub_msg_nb = 0};
    _node_ctx_t node_ctx_2 = {.keyexpr_out = "test/C", .id = 2, .qybl_msg_nb = 0, .sub_msg_nb = 0};
    _node_ctx_t node_ctx_3 = {.keyexpr_out = "test/D", .id = 3, .qybl_msg_nb = 0, .sub_msg_nb = 0};
    // Init config
    z_config_default(&node_ctx_0.config);
    z_config_default(&node_ctx_1.config);
    z_config_default(&node_ctx_2.config);
    z_config_default(&node_ctx_3.config);
    // Fill config
    zp_config_insert(z_loan_mut(node_ctx_0.config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(node_ctx_0.config), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:7447");
    zp_config_insert(z_loan_mut(node_ctx_1.config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(node_ctx_1.config), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:7448");
    zp_config_insert(z_loan_mut(node_ctx_1.config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7447");
    zp_config_insert(z_loan_mut(node_ctx_2.config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(node_ctx_2.config), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:7449");
    zp_config_insert(z_loan_mut(node_ctx_2.config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7447");
    zp_config_insert(z_loan_mut(node_ctx_2.config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7448");
    zp_config_insert(z_loan_mut(node_ctx_3.config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(node_ctx_3.config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7447");
    zp_config_insert(z_loan_mut(node_ctx_3.config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7448");
    zp_config_insert(z_loan_mut(node_ctx_3.config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7449");
    // Init threads in a staggered manner to let time for sockets to establish
    _z_task_t task0, task1, task2, task3;
    _z_task_init(&task0, NULL, node_task, &node_ctx_0);
    z_sleep_ms(100);
    _z_task_init(&task1, NULL, node_task, &node_ctx_1);
    z_sleep_ms(100);
    _z_task_init(&task2, NULL, node_task, &node_ctx_2);
    z_sleep_ms(100);
    _z_task_init(&task3, NULL, node_task, &node_ctx_3);
    // Wait a bit
    z_sleep_s(30);
    // Clean up
    _z_task_join(&task0);
    _z_task_join(&task1);
    _z_task_join(&task2);
    _z_task_join(&task3);
}

static void z_id_closure_count_peers(const z_id_t *id, void *ctx) {
    _ZP_UNUSED(id);
    int *count = (int *)ctx;
    (*count)++;
}

static int get_peers_count(const z_loaned_session_t *zs) {
    int peer_count = 0;
    z_owned_closure_zid_t id_closure;
    z_closure(&id_closure, z_id_closure_count_peers, NULL, &peer_count);
    z_info_peers_zid(zs, z_move(id_closure));
    return peer_count;
}

static bool test_many_to_one_peer_connection(void) {
    printf("Testing many-to-one peer connection...\n");
    // Init config
    z_owned_config_t config;
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:7447");
    // Open main session
    z_owned_session_t s;
    if (z_open(&s, z_move(config), NULL) != Z_OK) {
        printf("Unable to open main session!\n");
        return false;
    }

    z_owned_session_t sess_array[Z_MAX_NUM_UNICAST_PEERS];
    z_owned_config_t cfg_array[Z_MAX_NUM_UNICAST_PEERS];
    // // Open max peers
    for (int i = 0; i < Z_MAX_NUM_UNICAST_PEERS; i++) {
        z_config_default(&cfg_array[i]);
        zp_config_insert(z_loan_mut(cfg_array[i]), Z_CONFIG_MODE_KEY, "peer");
        zp_config_insert(z_loan_mut(cfg_array[i]), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:7447");
        cfg_array[i]._val._connect_exit_on_failure._parsed = true;
        if (z_open(&sess_array[i], z_move(cfg_array[i]), NULL) != Z_OK) {
            printf("Unable to open peer session!\n");
            return false;
        }
        z_sleep_ms(100);
        int peer_count = get_peers_count(z_loan(sess_array[i]));
        if (peer_count != 1) {
            printf("Failed to maintain connection with main session, expected 1 peer, got %d\n", peer_count);
            return false;
        }
    }
    // Close first session
    z_drop(z_move(s));
    z_sleep_ms(500);
    // Attempt to reopen listen session to exercise peer reconnection logic
    z_config_default(&config);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:7447");
    if (z_open(&s, z_move(config), NULL) != Z_OK) {
        printf("Unable to open peer session!\n");
        return false;
    }
    z_sleep_ms(Z_MAX_NUM_UNICAST_PEERS * 2000);
    int peer_count = get_peers_count(z_loan(s));
    if (peer_count != Z_MAX_NUM_UNICAST_PEERS) {
        printf("Unable to reestablish connection on listen session with all the peers, expected %d peers, got %d\n",
               Z_MAX_NUM_UNICAST_PEERS, peer_count);
        return false;
    }
    z_drop(z_move(s));
    for (size_t i = 0; i < _ZP_ARRAY_SIZE(sess_array); i++) {
        z_drop(z_move(sess_array[i]));
    }
    return true;
}

static bool test_full_mesh_peer_connection(size_t timeout_on_connct_ms) {
    printf("Testing full mesh peer connection (connect timeout on session open: %zu ms)...\n", timeout_on_connct_ms);
    z_clock_t test_start_time = z_clock_now();
    char locators[Z_MAX_NUM_UNICAST_PEERS + 1][32] = {0};
    size_t start_port = 10000;
    for (size_t i = 0; i < Z_MAX_NUM_UNICAST_PEERS + 1; i++) {
        sprintf(locators[i], "tcp/127.0.0.1:%zu", start_port + i);
    }
    z_owned_session_t sess_array[Z_MAX_NUM_UNICAST_PEERS + 1];
    for (size_t i = 0; i < _ZP_ARRAY_SIZE(sess_array); i++) {
        z_owned_config_t cfg;
        z_config_default(&cfg);
        zp_config_insert(z_loan_mut(cfg), Z_CONFIG_MODE_KEY, "peer");
        zp_config_insert(z_loan_mut(cfg), Z_CONFIG_LISTEN_KEY, locators[i]);
        cfg._val._connect_timeout._parsed = (uint32_t)timeout_on_connct_ms;

        // Connect only to lower-index peers so each pair has a single connect direction. This avoids
        // symmetric (mutual) connects and halves the number of connection attempts to N*(N+1)/2.
        for (size_t j = 0; j < i; j++) {
            zp_config_insert(z_loan_mut(cfg), Z_CONFIG_CONNECT_KEY, locators[j]);
        }
        if (z_open(&sess_array[i], z_move(cfg), NULL) != Z_OK) {
            printf("Unable to open peer session!\n");
            return false;
        }
    }
    z_clock_t start_time = z_clock_now();
    int num_connections = 0;
    const unsigned long max_duration_ms = Z_MAX_NUM_UNICAST_PEERS * 1000;
    bool ret = false;
    while (z_clock_elapsed_ms(&start_time) < max_duration_ms) {
        num_connections = 0;
        for (size_t i = 0; i < _ZP_ARRAY_SIZE(sess_array); i++) {
            num_connections += get_peers_count(z_loan(sess_array[i]));
        }
        if (num_connections == Z_MAX_NUM_UNICAST_PEERS * _ZP_ARRAY_SIZE(sess_array)) {
            ret = true;
            break;
        }
        printf("Established %d/%d connections after %zu ms\n", num_connections,
               (int)(Z_MAX_NUM_UNICAST_PEERS * _ZP_ARRAY_SIZE(sess_array)), (size_t)z_clock_elapsed_ms(&start_time));
        z_sleep_ms(1000);
    }
    if (!ret) {
        printf("Unable to establish full mesh connection with all the peers, expected %d connections, got %d\n",
               (int)(Z_MAX_NUM_UNICAST_PEERS * _ZP_ARRAY_SIZE(sess_array)), num_connections);
    } else {
        printf("Successfully established full mesh connection with all %zu peers in %zu ms\n",
               (size_t)_ZP_ARRAY_SIZE(sess_array), (size_t)z_clock_elapsed_ms(&test_start_time));
    }
    for (size_t i = 0; i < _ZP_ARRAY_SIZE(sess_array); i++) {
        z_drop(z_move(sess_array[i]));
    }
    return ret;
}

static bool test_self_connection_rejection(void) {
    printf("Testing self connection rejection...\n");
    z_owned_config_t cfg;
    z_config_default(&cfg);
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:10100");
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:10100");
    z_owned_session_t s;
    if (z_open(&s, z_move(cfg), NULL) != Z_OK) {
        printf("Unable to open peer session!\n");
        return false;
    }
    z_sleep_ms(1000);
    int peer_count = get_peers_count(z_loan(s));
    if (peer_count != 0) {
        printf("Self connection was not rejected, expected 0 peers, got %d\n", peer_count);
        return false;
    }
    z_drop(z_move(s));
    return true;
}

static bool test_duplicate_connection_rejection(void) {
    printf("Testing duplicate connection rejection...\n");
    z_owned_config_t cfg;
    z_config_default(&cfg);
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:10200");
    z_owned_session_t s;
    if (z_open(&s, z_move(cfg), NULL) != Z_OK) {
        printf("Unable to open peer session!\n");
        return false;
    }

    z_config_default(&cfg);
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:10200");
    zp_config_insert(z_loan_mut(cfg), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:10200");

    z_owned_session_t s2;
    if (z_open(&s2, z_move(cfg), NULL) != Z_OK) {
        printf("Unable to open peer session!\n");
        return false;
    }

    z_sleep_ms(1000);
    int peer_count = get_peers_count(z_loan(s));
    if (peer_count != 1) {
        printf("Duplicate connection was not rejected, expected 1 peer, got %d\n", peer_count);
        return false;
    }
    z_drop(z_move(s2));
    z_drop(z_move(s));
    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    test_packet_transmission();
    if (!test_many_to_one_peer_connection()) {
        return -1;
    }
    if (!test_full_mesh_peer_connection(1)) {
        return -1;
    }
    if (!test_full_mesh_peer_connection(5000)) {
        return -1;
    }
    if (!test_self_connection_rejection()) {
        return -1;
    }
    if (!test_duplicate_connection_rejection()) {
        return -1;
    }
    printf("All tests passed\n");
    return 0;
}

#else

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf(
        "Missing config token to build this test. This test requires: Z_FEATURE_SUBSCRIPTION, Z_FEATURE_PUBLICATION, "
        "Z_FEATURE_QUERY, Z_FEATURE_QUERYABLE, Z_FEATURE_MULTI_THREAD, Z_FEATURE_UNICAST_PEER\n");
    return 0;
}

#endif
