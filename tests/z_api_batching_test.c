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

#include <assert.h>
#include <string.h>

#include "utils/assert_helpers.h"
#include "zenoh-pico.h"

#if Z_FEATURE_BATCHING == 1

#if Z_FEATURE_PUBLICATION == 1 && Z_FEATURE_SUBSCRIPTION == 1 && Z_FEATURE_MULTI_THREAD == 1

static z_result_t recv_sample_with_timeout(const z_loaned_fifo_handler_sample_t *handler, z_owned_sample_t *sample) {
    z_result_t ret = Z_CHANNEL_NODATA;
    for (size_t attempt = 0; attempt < 50; ++attempt) {
        ret = z_fifo_handler_sample_try_recv(handler, sample);
        if (ret != Z_CHANNEL_NODATA) {
            break;
        }
        z_sleep_ms(100);
    }
    return ret;
}

void test_batched_publications_arrive(void) {
    printf("test_batched_publications_arrive\n");
    static const char *const payloads[] = {"first", "a-longer-second-message", "third-message"};

    z_owned_session_t publisher_session, subscriber_session;
    z_owned_config_t publisher_config, subscriber_config;
    z_config_default(&publisher_config);
    z_config_default(&subscriber_config);

    zp_config_insert(z_loan_mut(subscriber_config), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(subscriber_config), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:12346");
    zp_config_insert(z_loan_mut(publisher_config), Z_CONFIG_MODE_KEY, "client");
    zp_config_insert(z_loan_mut(publisher_config), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:12346");

    ASSERT_OK(z_open(&subscriber_session, z_move(subscriber_config), NULL));
    ASSERT_OK(z_open(&publisher_session, z_move(publisher_config), NULL));

    z_view_keyexpr_t keyexpr;
    ASSERT_OK(z_view_keyexpr_from_str(&keyexpr, "zenoh-pico/batching/test"));

    z_owned_closure_sample_t callback;
    z_owned_fifo_handler_sample_t handler;
    ASSERT_OK(z_fifo_channel_sample_new(&callback, &handler, sizeof(payloads) / sizeof(payloads[0])));

    z_owned_subscriber_t subscriber;
    ASSERT_OK(z_declare_subscriber(z_loan(subscriber_session), &subscriber, z_loan(keyexpr), z_move(callback), NULL));

    z_owned_publisher_t publisher;
    ASSERT_OK(z_declare_publisher(z_loan(publisher_session), &publisher, z_loan(keyexpr), NULL));

    z_sleep_ms(1000);  // Wait for the connection and subscription declaration to propagate

    ASSERT_OK(zp_batch_start(z_loan_mut(publisher_session)));
    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); ++i) {
        z_owned_bytes_t payload;
        ASSERT_OK(z_bytes_copy_from_str(&payload, payloads[i]));
        ASSERT_OK(z_publisher_put(z_loan(publisher), z_move(payload), NULL));
    }

    z_owned_sample_t sample;
    ASSERT_TRUE(z_fifo_handler_sample_try_recv(z_loan(handler), &sample) == Z_CHANNEL_NODATA);

    ASSERT_OK(zp_batch_flush(z_loan_mut(publisher_session)));

    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); ++i) {
        ASSERT_OK(recv_sample_with_timeout(z_loan(handler), &sample));

        z_owned_string_t received;
        ASSERT_OK(z_bytes_to_string(z_sample_payload(z_loan(sample)), &received));
        ASSERT_TRUE(z_string_len(z_loan(received)) == strlen(payloads[i]));
        ASSERT_TRUE(memcmp(z_string_data(z_loan(received)), payloads[i], strlen(payloads[i])) == 0);

        z_drop(z_move(received));
        z_drop(z_move(sample));
    }

    ASSERT_TRUE(z_fifo_handler_sample_try_recv(z_loan(handler), &sample) == Z_CHANNEL_NODATA);
    ASSERT_OK(zp_batch_stop(z_loan_mut(publisher_session)));

    ASSERT_OK(z_undeclare_publisher(z_move(publisher)));
    ASSERT_OK(z_undeclare_subscriber(z_move(subscriber)));
    z_drop(z_move(handler));
    z_session_drop(z_session_move(&publisher_session));
    z_session_drop(z_session_move(&subscriber_session));
}

#endif

void test_batching_while_connected(void) {
    printf("test_batching_while_connected\n");
    z_owned_session_t s1, s2;
    z_owned_config_t c1, c2;
    z_config_default(&c1);
    z_config_default(&c2);

    ASSERT_OK(z_open(&s1, z_move(c1), NULL));
    ASSERT_OK(z_open(&s2, z_move(c2), NULL));

    z_sleep_ms(1000);  // Wait for connection to establish

    ASSERT_OK(zp_batch_start(z_loan_mut(s2)));
    ASSERT_OK(zp_batch_flush(z_loan_mut(s2)));
    ASSERT_OK(zp_batch_stop(z_loan_mut(s2)));

    z_session_drop(z_session_move(&s1));
    z_session_drop(z_session_move(&s2));
}

void test_batching_after_disconnection(void) {
    printf("test_batching_after_disconnection\n");
    z_owned_session_t s1, s2;
    z_owned_config_t c1, c2;
    z_config_default(&c1);
    z_config_default(&c2);

    zp_config_insert(z_loan_mut(c1), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(c1), Z_CONFIG_LISTEN_KEY, "tcp/127.0.0.1:12345");

    zp_config_insert(z_loan_mut(c2), Z_CONFIG_MODE_KEY, "client");
    zp_config_insert(z_loan_mut(c2), Z_CONFIG_CONNECT_KEY, "tcp/127.0.0.1:12345");

    ASSERT_OK(z_open(&s1, z_move(c1), NULL));
    ASSERT_OK(z_open(&s2, z_move(c2), NULL));

    // Wait for connection to establish
    z_sleep_ms(1000);

    // Drop session and wait for it to be detected as dropped
    z_session_drop(z_session_move(&s1));
    z_sleep_ms(10000);

    // Connection is not required for batching to work
    ASSERT_OK(zp_batch_start(z_loan_mut(s2)));
    ASSERT_OK(zp_batch_flush(z_loan_mut(s2)));
    ASSERT_OK(zp_batch_stop(z_loan_mut(s2)));

    z_session_drop(z_session_move(&s2));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
#if Z_FEATURE_PUBLICATION == 1 && Z_FEATURE_SUBSCRIPTION == 1 && Z_FEATURE_MULTI_THREAD == 1
    test_batched_publications_arrive();
#endif
    test_batching_while_connected();
    test_batching_after_disconnection();

    return 0;
}

#else

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Missing config token to build this test. This test requires: Z_FEATURE_BATCHING\n");
    return 0;
}

#endif  // Z_FEATURE_BATCHING == 1
