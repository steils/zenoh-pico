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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#undef NDEBUG
#include <assert.h>

#include "zenoh-pico/collections/algorithms_template.h"

// ── Instantiate int min-heap, capacity 8 ─────────────────────────────────────

static inline int intpq_cmp(const int *a, const int *b) { return *a - *b; }
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE int
#define _ZP_STATIC_PQUEUE_TEMPLATE_NAME intpq
#define _ZP_STATIC_PQUEUE_TEMPLATE_SIZE 8
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN intpq_cmp
#include "zenoh-pico/collections/static_pqueue_template.h"

// ── Instantiate int heap with context-carried comparator ─────────────────────
// The context holds a multiplier: +1 → min-heap, -1 → max-heap.

typedef struct {
    int multiplier;
} intpq_cmp_ctx_t;

static inline int intpq_with_ctx_cmp(const int *a, const int *b, const intpq_cmp_ctx_t *ctx) {
    return ctx->multiplier * (*a - *b);
}
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE int
#define _ZP_STATIC_PQUEUE_TEMPLATE_NAME intpq_with_ctx
#define _ZP_STATIC_PQUEUE_TEMPLATE_SIZE 8
#define _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE intpq_cmp_ctx_t
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN intpq_with_ctx_cmp
#include "zenoh-pico/collections/static_pqueue_template.h"

// ── Tests: context-free min-heap ─────────────────────────────────────────────

static void test_new_is_empty(void) {
    printf("Test: new queue is empty\n");
    intpq_t pq = intpq_new();
    assert(intpq_is_empty(&pq));
    assert(intpq_size(&pq) == 0);
    assert(intpq_peek(&pq) == NULL);
    intpq_destroy(&pq);
}

static void test_push_pop_single(void) {
    printf("Test: push then pop returns the same element\n");
    intpq_t pq = intpq_new();
    int v = 42;
    assert(intpq_push(&pq, &v));
    assert(intpq_size(&pq) == 1);
    int out = 0;
    assert(intpq_pop(&pq, &out));
    assert(out == 42);
    assert(intpq_is_empty(&pq));
    intpq_destroy(&pq);
}

static void test_min_heap_order(void) {
    printf("Test: pop returns elements in ascending order\n");
    intpq_t pq = intpq_new();
    int vals[] = {5, 1, 8, 3, 2, 7, 4, 6};
    for (int i = 0; i < 8; i++) {
        assert(intpq_push(&pq, &vals[i]));
    }
    assert(intpq_size(&pq) == 8);
    int prev = -1;
    for (int i = 0; i < 8; i++) {
        int out = 0;
        assert(intpq_pop(&pq, &out));
        assert(out > prev);
        prev = out;
    }
    assert(intpq_is_empty(&pq));
    intpq_destroy(&pq);
}

static void test_peek_does_not_remove(void) {
    printf("Test: peek returns min without removing it\n");
    intpq_t pq = intpq_new();
    int a = 10, b = 3, c = 7;
    assert(intpq_push(&pq, &a));
    assert(intpq_push(&pq, &b));
    assert(intpq_push(&pq, &c));
    int *top = intpq_peek(&pq);
    assert(top != NULL && *top == 3);
    assert(intpq_size(&pq) == 3);  // peek must not remove
    intpq_destroy(&pq);
}

static void test_capacity_exceeded(void) {
    printf("Test: push fails when capacity is full\n");
    intpq_t pq = intpq_new();
    for (int i = 0; i < 8; i++) {
        assert(intpq_push(&pq, &i));
    }
    assert(intpq_size(&pq) == 8);
    int extra = 99;
    assert(!intpq_push(&pq, &extra));  // must fail
    assert(intpq_size(&pq) == 8);
    intpq_destroy(&pq);
}

static void test_pop_on_empty_returns_false(void) {
    printf("Test: pop on empty queue returns false\n");
    intpq_t pq = intpq_new();
    int out = 0;
    assert(!intpq_pop(&pq, &out));
    intpq_destroy(&pq);
}

static void test_destroy_resets_size(void) {
    printf("Test: destroy resets size to zero\n");
    intpq_t pq = intpq_new();
    int v = 1;
    assert(intpq_push(&pq, &v));
    intpq_destroy(&pq);
    assert(intpq_size(&pq) == 0);
    assert(intpq_is_empty(&pq));
}

static void test_duplicate_values(void) {
    printf("Test: duplicate values are handled correctly\n");
    intpq_t pq = intpq_new();
    int vals[] = {3, 3, 1, 1, 2, 2};
    for (int i = 0; i < 6; i++) {
        assert(intpq_push(&pq, &vals[i]));
    }
    int out, prev = -1;
    int count = 0;
    while (intpq_pop(&pq, &out)) {
        assert(out >= prev);
        prev = out;
        count++;
    }
    assert(count == 6);
    intpq_destroy(&pq);
}

static void test_push_pop_interleaved(void) {
    printf("Test: interleaved push and pop maintains heap property\n");
    intpq_t pq = intpq_new();
    // Push 3, pop min, push 1, pop min, etc.
    int vals[] = {5, 3, 8, 1, 4};
    int expected[] = {3, 1, 4};  // after: push 5,3 pop→3; push 8,1 pop→1; push 4 pop→4
    int v;

    v = 5;
    intpq_push(&pq, &v);
    v = 3;
    intpq_push(&pq, &v);
    int out = 0;
    assert(intpq_pop(&pq, &out) && out == expected[0]);

    v = 8;
    intpq_push(&pq, &v);
    v = 1;
    intpq_push(&pq, &v);
    assert(intpq_pop(&pq, &out) && out == expected[1]);

    v = 4;
    intpq_push(&pq, &v);
    assert(intpq_pop(&pq, &out) && out == expected[2]);

    (void)vals;
    intpq_destroy(&pq);
}

// ── Tests: context-aware (max-heap via multiplier = -1) ──────────────────────

static void test_ctx_max_heap_order(void) {
    printf("Test (ctx): pop returns elements in descending order (max-heap)\n");
    intpq_cmp_ctx_t ctx;
    ctx.multiplier = -1;
    intpq_with_ctx_t pq = intpq_with_ctx_new_with_ctx(&ctx);
    int vals[] = {5, 1, 8, 3, 2, 7, 4, 6};
    for (int i = 0; i < 8; i++) {
        assert(intpq_with_ctx_push(&pq, &vals[i]));
    }
    int prev = 9, out = 0;
    for (int i = 0; i < 8; i++) {
        assert(intpq_with_ctx_pop(&pq, &out));
        assert(out < prev);
        prev = out;
    }
    assert(intpq_with_ctx_is_empty(&pq));
    intpq_with_ctx_destroy(&pq);
}

static void test_ctx_new_zero_init(void) {
    printf("Test (ctx): new() zero-initialises context pointer (min-heap behaviour with NULL ctx)\n");
    // Context-free new() still works when CMP_CTX_TYPE is defined —
    // the context pointer is NULL.  Since our compare never dereferences a
    // NULL ctx (it uses the multiplier which is 0 → treats all elements as
    // equal), the heap doesn't crash; we only verify it doesn't segfault and
    // that size/empty behave correctly.
    intpq_with_ctx_t pq = intpq_with_ctx_new();
    assert(intpq_with_ctx_is_empty(&pq));
    intpq_with_ctx_destroy(&pq);
}

static void test_ctx_set_ctx(void) {
    printf("Test (ctx): set_ctx switches comparison context on existing queue\n");
    intpq_cmp_ctx_t min_ctx;
    min_ctx.multiplier = 1;
    intpq_cmp_ctx_t max_ctx;
    max_ctx.multiplier = -1;

    // Start as min-heap
    intpq_with_ctx_t pq = intpq_with_ctx_new_with_ctx(&min_ctx);
    int vals[] = {4, 2, 6};
    for (int i = 0; i < 3; i++) intpq_with_ctx_push(&pq, &vals[i]);

    int out = 0;
    assert(intpq_with_ctx_pop(&pq, &out) && out == 2);  // min first

    // Drain remaining, switch to max-heap, reload
    intpq_with_ctx_destroy(&pq);
    intpq_with_ctx_set_ctx(&pq, &max_ctx);
    for (int i = 0; i < 3; i++) intpq_with_ctx_push(&pq, &vals[i]);

    assert(intpq_with_ctx_pop(&pq, &out) && out == 6);  // max first
    intpq_with_ctx_destroy(&pq);
}

// ── Shared helpers ──────────────────────────────────────────────────────────

static int int_compare_qsort(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

// Drain @p pq into @p out (must have room for at least size() entries) and
// return the number of elements popped.
static int drain_intpq(intpq_t *pq, int *out) {
    int n = 0;
    while (intpq_pop(pq, &out[n])) n++;
    return n;
}

// ── Tests: iteration ─────────────────────────────────────────────────────────

static void test_iter_empty_queue(void) {
    printf("Test (iter): begin() == end() for empty queue\n");
    intpq_t pq = intpq_new();
    assert(intpq_begin(&pq) == intpq_end(&pq));
    intpq_destroy(&pq);
}

static void test_iter_visits_all_elements(void) {
    printf("Test (iter): forward iteration visits every element exactly once\n");
    intpq_t pq = intpq_new();
    int vals[] = {5, 1, 8, 3, 2, 7};
    for (int i = 0; i < 6; i++) intpq_push(&pq, &vals[i]);

    int collected[6];
    int n = 0;
    for (intpq_iter_t it = intpq_begin(&pq); it != intpq_end(&pq); it = intpq_iter_next(&pq, it)) {
        collected[n++] = *intpq_at(&pq, it);
    }
    assert(n == 6);

    int expected[] = {1, 2, 3, 5, 7, 8};
    qsort(collected, 6, sizeof(int), int_compare_qsort);
    for (int i = 0; i < 6; i++) assert(collected[i] == expected[i]);

    intpq_destroy(&pq);
}

static void test_iter_const_at(void) {
    printf("Test (iter): const_at returns a correct read-only pointer\n");
    intpq_t pq = intpq_new();
    int v = 42;
    intpq_push(&pq, &v);
    const intpq_t *cpq = &pq;
    assert(*intpq_const_at(cpq, 0) == 42);
    intpq_destroy(&pq);
}

static void test_iter_get_bounds(void) {
    printf("Test (iter): get() returns NULL for out-of-bounds index\n");
    intpq_t pq = intpq_new();
    int v = 10;
    intpq_push(&pq, &v);
    assert(intpq_get(&pq, 0) != NULL);
    assert(intpq_get(&pq, 1) == NULL);  // index == size is out of bounds
    intpq_destroy(&pq);
}

// ── Tests: remove_at ─────────────────────────────────────────────────────────

static void test_remove_at_out_of_bounds(void) {
    printf("Test (remove_at): returns false when idx is out of bounds\n");
    intpq_t pq = intpq_new();
    intpq_iter_t next;
    assert(!intpq_remove_at(&pq, 0, NULL, &next));  // empty queue
    int v = 7;
    intpq_push(&pq, &v);
    assert(!intpq_remove_at(&pq, 1, NULL, &next));  // idx == size
    intpq_destroy(&pq);
}

static void test_remove_at_single_element(void) {
    printf("Test (remove_at): single-element queue becomes empty\n");
    intpq_t pq = intpq_new();
    int v = 5;
    intpq_push(&pq, &v);
    intpq_iter_t next;
    assert(intpq_remove_at(&pq, 0, NULL, &next));
    assert(intpq_is_empty(&pq));
    assert(next == intpq_end(&pq));
    intpq_destroy(&pq);
}

static void test_remove_at_with_out(void) {
    printf("Test (remove_at): non-NULL out receives the removed element\n");
    intpq_t pq = intpq_new();
    int vals[] = {5, 1, 8, 3};
    for (int i = 0; i < 4; i++) intpq_push(&pq, &vals[i]);

    // Locate 8 in the buffer via iteration
    intpq_iter_t idx_8 = intpq_end(&pq);
    for (intpq_iter_t it = intpq_begin(&pq); it != intpq_end(&pq); it = intpq_iter_next(&pq, it)) {
        if (*intpq_at(&pq, it) == 8) {
            idx_8 = it;
            break;
        }
    }
    assert(idx_8 != intpq_end(&pq));

    int removed = 0;
    intpq_iter_t next;
    assert(intpq_remove_at(&pq, idx_8, &removed, &next));
    assert(removed == 8);
    assert(intpq_size(&pq) == 3);

    // Heap must still be valid: pop order ascending
    int buf[4];
    int n = drain_intpq(&pq, buf);
    assert(n == 3);
    for (int i = 1; i < n; i++) assert(buf[i] >= buf[i - 1]);
    intpq_destroy(&pq);
}

static void test_remove_at_heap_preserved(void) {
    printf("Test (remove_at): heap property is preserved after removal from the middle\n");
    intpq_t pq = intpq_new();
    int vals[] = {5, 1, 8, 3, 2, 7, 4, 6};
    for (int i = 0; i < 8; i++) intpq_push(&pq, &vals[i]);

    // Remove from index 3 (arbitrary mid-heap position)
    assert(intpq_remove_at(&pq, 3, NULL, NULL));
    assert(intpq_size(&pq) == 7);

    int buf[8];
    int n = drain_intpq(&pq, buf);
    assert(n == 7);
    for (int i = 1; i < n; i++) assert(buf[i] >= buf[i - 1]);
    intpq_destroy(&pq);
}

static void test_remove_at_last_slot_sets_next_to_end(void) {
    printf("Test (remove_at): removing the last slot sets next_idx to end()\n");
    intpq_t pq = intpq_new();
    int vals[] = {3, 1, 2};
    for (int i = 0; i < 3; i++) intpq_push(&pq, &vals[i]);
    size_t last = intpq_size(&pq) - 1;
    intpq_iter_t next;
    assert(intpq_remove_at(&pq, last, NULL, &next));
    assert(next == intpq_end(&pq));
    intpq_destroy(&pq);
}

// ── Tests: algorithms macros ──────────────────────────────────────────────────

static void test_foreach_macro(void) {
    printf("Test (_ZP_FOREACH): visits all elements, sum is correct\n");
    intpq_t pq = intpq_new();
    int vals[] = {1, 2, 3, 4, 5};
    for (int i = 0; i < 5; i++) intpq_push(&pq, &vals[i]);

    int sum = 0;
    int *elem = NULL;
    _ZP_FOREACH (intpq, &pq, elem) {
        sum += *elem;
    }
    assert(sum == 15);
    intpq_destroy(&pq);
}

static void test_const_foreach_macro(void) {
    printf("Test (_ZP_CONST_FOREACH): iterates a const-qualified queue\n");
    intpq_t pq = intpq_new();
    int vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) intpq_push(&pq, &vals[i]);

    int count = 0;
    const int *elem = NULL;
    _ZP_CONST_FOREACH (intpq, &pq, elem) {
        (void)elem;
        count++;
    }
    assert(count == 3);
    intpq_destroy(&pq);
}

static void test_find_macro(void) {
    printf("Test (_ZP_FIND): returns matching element or NULL\n");
    intpq_t pq = intpq_new();
    int vals[] = {5, 1, 8, 3, 2};
    for (int i = 0; i < 5; i++) intpq_push(&pq, &vals[i]);

    int *found;
    _ZP_FIND(intpq, &pq, found, (*_ == 3));
    assert(found != NULL && *found == 3);

    _ZP_FIND(intpq, &pq, found, (*_ == 99));
    assert(found == NULL);

    intpq_destroy(&pq);
}

static void test_const_find_macro(void) {
    printf("Test (_ZP_CONST_FIND): locates an element in a const-qualified queue\n");
    intpq_t pq = intpq_new();
    int v = 7;
    intpq_push(&pq, &v);

    const int *found;
    _ZP_CONST_FIND(intpq, &pq, found, (*_ == 7));
    assert(found != NULL && *found == 7);

    _ZP_CONST_FIND(intpq, &pq, found, (*_ == 99));
    assert(found == NULL);

    intpq_destroy(&pq);
}

static void test_remove_macro_basic(void) {
    printf("Test (_ZP_REMOVE): removes all even numbers; heap property preserved\n");
    intpq_t pq = intpq_new();
    int vals[] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (int i = 0; i < 8; i++) intpq_push(&pq, &vals[i]);

    _ZP_REMOVE_ALL(intpq, &pq, (*_ % 2 == 0));

    int buf[8];
    int n = drain_intpq(&pq, buf);
    assert(n == 4);  // 1, 3, 5, 7
    for (int i = 0; i < n; i++) assert(buf[i] % 2 != 0);
    for (int i = 1; i < n; i++) assert(buf[i] >= buf[i - 1]);
    intpq_destroy(&pq);
}

static void test_remove_macro_sift_up_correctness(void) {
    // Critical regression test: when remove_at causes the replacement (last
    // element) to sift UP past the current iterator, remove_at must set
    // next_idx to the new position so _ZP_REMOVE does not skip the element.
    //
    // Push order 2,5,3,8,9,7,1 yields heap buffer [1,5,2,8,9,7,3].
    // Removing 8 (index 3) moves element 3 (last) into slot 3, which then
    // sifts up to index 1.  Without the k<idx correction, _ZP_REMOVE would
    // resume at index 3 and miss the value 3 that landed at index 1.
    printf("Test (_ZP_REMOVE): elements that sift up past iterator are not missed\n");
    intpq_t pq = intpq_new();
    int push_order[] = {2, 5, 3, 8, 9, 7, 1};
    for (int i = 0; i < 7; i++) intpq_push(&pq, &push_order[i]);

    _ZP_REMOVE_ALL(intpq, &pq, (*_ == 8 || *_ == 3));

    int buf[8];
    int n = drain_intpq(&pq, buf);
    for (int i = 0; i < n; i++) {
        assert(buf[i] != 8 && buf[i] != 3);
    }
    intpq_destroy(&pq);
}

int main(void) {
    // Context-free min-heap tests
    test_new_is_empty();
    test_push_pop_single();
    test_min_heap_order();
    test_peek_does_not_remove();
    test_capacity_exceeded();
    test_pop_on_empty_returns_false();
    test_destroy_resets_size();
    test_duplicate_values();
    test_push_pop_interleaved();

    // Context-aware tests
    test_ctx_max_heap_order();
    test_ctx_new_zero_init();
    test_ctx_set_ctx();

    // Iteration tests
    test_iter_empty_queue();
    test_iter_visits_all_elements();
    test_iter_const_at();
    test_iter_get_bounds();

    // remove_at tests
    test_remove_at_out_of_bounds();
    test_remove_at_single_element();
    test_remove_at_with_out();
    test_remove_at_heap_preserved();
    test_remove_at_last_slot_sets_next_to_end();

    // Algorithms macro tests
    test_foreach_macro();
    test_const_foreach_macro();
    test_find_macro();
    test_const_find_macro();
    test_remove_macro_basic();
    test_remove_macro_sift_up_correctness();

    printf("All pqueue tests passed.\n");
    return 0;
}
