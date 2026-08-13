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
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zenoh-pico.h"
#include "zenoh-pico/protocol/codec.h"

#define TEST_RUN_TIME_S 20

typedef struct {
    unsigned long frequency;
    _Atomic unsigned long count;
    bool *stop_flag;
} z_stats_t;

#if Z_FEATURE_PUBLICATION == 1 && Z_FEATURE_MULTI_THREAD == 1

static int parse_args(int argc, char **argv, z_owned_config_t *config, char **ke, unsigned long *freq, size_t *pkt_size,
                      bool *batching);

unsigned long cas_loop(z_stats_t *ctx, unsigned long value) {
    unsigned long prev_val = atomic_load_explicit(&ctx->count, memory_order_relaxed);
    unsigned long curr_val = prev_val;
    while (curr_val != value) {
        prev_val = curr_val;
        if (atomic_compare_exchange_weak_explicit(&ctx->count, &curr_val, value, memory_order_acquire,
                                                  memory_order_relaxed)) {
            return prev_val;
        }
    }
    return prev_val;
}

void *measure_task(void *ctx) {
    z_stats_t *stats = (z_stats_t *)ctx;
    unsigned long sleep_dur_us = 1000000 / stats->frequency;
    while (!*stats->stop_flag) {
        z_clock_t start = z_clock_now();
        z_sleep_us(sleep_dur_us);
        unsigned long elapsed = z_clock_elapsed_us(&start);
        // Clear count
        unsigned long count = cas_loop(stats, 0);
        if (count > 0) {
            printf("%.3lf\n", (double)count * 1000000.0 / (double)elapsed);
        }
    }
    return NULL;
}

void *stop_task(void *ctx) {
    z_sleep_s(TEST_RUN_TIME_S);
    bool *stop_flag = (bool *)ctx;
    *stop_flag = true;
    return NULL;
}

int main(int argc, char **argv) {
    char *keyexpr = "thr";
    size_t len = 8;

    z_stats_t *context = malloc(sizeof(z_stats_t));
    atomic_store_explicit(&context->count, 0, memory_order_relaxed);
    context->frequency = 10;

    // Set config
    z_owned_config_t config;
    z_config_default(&config);
    bool batching = false;

    int ret = parse_args(argc, argv, &config, &keyexpr, &context->frequency, &len, &batching);
    if (ret != 0) {
        return ret;
    }

    // Open session
    z_owned_session_t s;
    if (z_open(&s, z_move(config), NULL) < 0) {
        printf("Unable to open session!\n");
        exit(-1);
    }

    // Timer for sub to prepare
    z_sleep_s(1);
    // Declare publisher
    z_owned_publisher_t pub;
    z_view_keyexpr_t ke;
    if (z_view_keyexpr_from_str(&ke, keyexpr) < 0) {
        printf("%s is not a valid key expression\n", keyexpr);
        return -1;
    }
    z_publisher_options_t opts;
    z_publisher_options_default(&opts);
    opts.congestion_control = Z_CONGESTION_CONTROL_DROP;
    if (z_declare_publisher(z_loan(s), &pub, z_loan(ke), &opts) < 0) {
        printf("Unable to declare publisher for key expression!\n");
        exit(-1);
    }

    uint8_t *value = (uint8_t *)z_malloc(len);
    memset(value, 1, len);

    bool stop_flag = false;
    context->stop_flag = &stop_flag;
    pthread_t task;
    pthread_create(&task, NULL, measure_task, (void *)context);
    pthread_t task2;
    pthread_create(&task2, NULL, stop_task, &stop_flag);

    // Send packets
#if Z_FEATURE_BATCHING == 1
    if (batching) zp_batch_start(z_loan(s));
#endif
    while (!stop_flag) {
        z_owned_bytes_t payload;
        z_bytes_from_static_buf(&payload, value, len);
        z_publisher_put(z_loan(pub), z_move(payload), NULL);
        atomic_fetch_add_explicit(&context->count, 1, memory_order_relaxed);
    }
#if Z_FEATURE_BATCHING == 1
    if (batching) zp_batch_stop(z_loan(s));
#endif

    // Clean up
    pthread_join(task, NULL);
    pthread_join(task2, NULL);
    z_drop(z_move(pub));
    z_drop(z_move(s));
    z_free(value);
    z_free(context);
    exit(0);
}

// Note: All args can be specified multiple times. For "-e" it will append the list of endpoints, for the other it will
// simply replace the previous value.
static int parse_args(int argc, char **argv, z_owned_config_t *config, char **ke, unsigned long *freq, size_t *pkt_size,
                      bool *batching) {
    int opt;
    while ((opt = getopt(argc, argv, "k:e:m:l:f:s:b")) != -1) {
        z_result_t ret = Z_OK;
        switch (opt) {
            case 'b':
                *batching = true;
                break;
            case 'k':
                *ke = optarg;
                break;
            case 'e':
                ret = zp_config_insert(z_loan_mut(*config), Z_CONFIG_CONNECT_KEY, optarg);
                break;
            case 'm':
                ret = zp_config_insert(z_loan_mut(*config), Z_CONFIG_MODE_KEY, optarg);
                break;
            case 'l':
                ret = zp_config_insert(z_loan_mut(*config), Z_CONFIG_LISTEN_KEY, optarg);
                break;
            case 'f':
                *freq = (unsigned long)atoi(optarg);
                break;
            case 's':
                *pkt_size = (size_t)atoi(optarg);
                break;
            case '?':
                if (optopt == 'k' || optopt == 'e' || optopt == 'm' || optopt == 'l' || optopt == 'f' ||
                    optopt == 's') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                }
                return 1;
            default:
                ret = _Z_ERR_INVALID;
        }
        if (ret != Z_OK) {
            fprintf(stderr, "Failed to set config option for -%c: %d\n", opt, ret);
            return 1;
        }
    }
    return 0;
}

#else
int main(void) {
    printf("ERROR: Zenoh pico was compiled without Z_FEATURE_PUBLICATION but this example requires it.\n");
    return -2;
}
#endif
