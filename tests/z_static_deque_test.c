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

#undef NDEBUG
#include <assert.h>

// ── Instantiate int deque, capacity 8 ────────────────────────────────────────

#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE int
#define _ZP_STATIC_DEQUE_TEMPLATE_NAME intdeque
#define _ZP_STATIC_DEQUE_TEMPLATE_SIZE 8
#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/collections/static_deque_template.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static void test_new_is_empty(void) {
    printf("Test: new deque is empty\n");
    intdeque_t d = intdeque_new();
    assert(intdeque_is_empty(&d));
    assert(intdeque_size(&d) == 0);
    assert(intdeque_front(&d) == NULL);
    assert(intdeque_back(&d) == NULL);
    intdeque_destroy(&d);
}

static void test_push_back_pop_front(void) {
    printf("Test: push_back then pop_front preserves FIFO order\n");
    intdeque_t d = intdeque_new();
    int vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        assert(intdeque_push_back(&d, &vals[i]));
    }
    assert(intdeque_size(&d) == 3);
    for (int i = 0; i < 3; i++) {
        int out = -1;
        assert(intdeque_pop_front(&d, &out));
        assert(out == vals[i]);
    }
    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_push_front_pop_front(void) {
    printf("Test: push_front then pop_front preserves stack (LIFO) order\n");
    intdeque_t d = intdeque_new();
    int vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        assert(intdeque_push_front(&d, &vals[i]));
    }
    assert(intdeque_size(&d) == 3);
    for (int i = 2; i >= 0; i--) {
        int out = -1;
        assert(intdeque_pop_front(&d, &out));
        assert(out == vals[i]);
    }
    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_push_back_pop_back(void) {
    printf("Test: push_back then pop_back preserves stack (LIFO) order\n");
    intdeque_t d = intdeque_new();
    int vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        assert(intdeque_push_back(&d, &vals[i]));
    }
    for (int i = 2; i >= 0; i--) {
        int out = -1;
        assert(intdeque_pop_back(&d, &out));
        assert(out == vals[i]);
    }
    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_push_front_pop_back(void) {
    printf("Test: push_front then pop_back preserves FIFO order\n");
    intdeque_t d = intdeque_new();
    int vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) {
        assert(intdeque_push_front(&d, &vals[i]));
    }
    for (int i = 0; i < 3; i++) {
        int out = -1;
        assert(intdeque_pop_back(&d, &out));
        assert(out == vals[i]);
    }
    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_front_back_peek(void) {
    printf("Test: front and back return correct pointers without removing\n");
    intdeque_t d = intdeque_new();
    int a = 1, b = 2, c = 3;
    assert(intdeque_push_back(&d, &a));
    assert(intdeque_push_back(&d, &b));
    assert(intdeque_push_back(&d, &c));
    assert(*intdeque_front(&d) == 1);
    assert(*intdeque_back(&d) == 3);
    assert(intdeque_size(&d) == 3);
    intdeque_destroy(&d);
}

static void test_capacity_full(void) {
    printf("Test: push fails when deque is full (capacity 8)\n");
    intdeque_t d = intdeque_new();
    int v = 0;
    for (int i = 0; i < 8; i++) {
        v = i;
        assert(intdeque_push_back(&d, &v));
    }
    assert(intdeque_size(&d) == 8);
    // Next push should fail
    v = 99;
    assert(!intdeque_push_back(&d, &v));
    assert(!intdeque_push_front(&d, &v));
    intdeque_destroy(&d);
}

static void test_pop_empty(void) {
    printf("Test: pop on empty deque returns false\n");
    intdeque_t d = intdeque_new();
    int out = -1;
    assert(!intdeque_pop_front(&d, &out));
    assert(!intdeque_pop_back(&d, &out));
    intdeque_destroy(&d);
}

static void test_wrap_around_back(void) {
    printf("Test: circular wrap-around with push_back/pop_front\n");
    intdeque_t d = intdeque_new();
    // Fill to 4, drain 4, then fill 8 — this forces wrap-around
    int v;
    for (int i = 0; i < 4; i++) {
        v = i;
        assert(intdeque_push_back(&d, &v));
    }
    for (int i = 0; i < 4; i++) {
        int out;
        assert(intdeque_pop_front(&d, &out));
        assert(out == i);
    }
    assert(intdeque_is_empty(&d));
    for (int i = 0; i < 8; i++) {
        v = 100 + i;
        assert(intdeque_push_back(&d, &v));
    }
    assert(intdeque_size(&d) == 8);
    for (int i = 0; i < 8; i++) {
        int out;
        assert(intdeque_pop_front(&d, &out));
        assert(out == 100 + i);
    }
    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_wrap_around_front(void) {
    printf("Test: circular wrap-around with push_front/pop_back\n");
    intdeque_t d = intdeque_new();
    int v;
    v = 200;
    for (int i = 0; i < 8; i++) {
        v = 200 + i;
        assert(intdeque_push_front(&d, &v));  // start walks down from SIZE
    }
    // Order from front: 207,206,...,201,200
    assert(intdeque_size(&d) == 8);
    for (int i = 7; i >= 1; i--) {
        int out;
        assert(intdeque_pop_front(&d, &out));
        assert(out == 200 + i);
    }
    int out;
    assert(intdeque_pop_front(&d, &out));
    assert(out == 200);
    assert(intdeque_is_empty(&d));

    // Also verify wrap-around pop_back ordering
    v = 300;
    assert(intdeque_push_back(&d, &v));
    for (int i = 1; i < 5; i++) {
        v = 300 + i;
        assert(intdeque_push_front(&d, &v));
    }
    // front->back: 304,303,302,301,300
    assert(intdeque_size(&d) == 5);
    assert(intdeque_pop_back(&d, &out));
    assert(out == 300);
    assert(intdeque_pop_front(&d, &out));
    assert(out == 304);
    intdeque_destroy(&d);
}

static void test_mixed_push_pop(void) {
    printf("Test: interleaved push_front/push_back and pop_front/pop_back\n");
    intdeque_t d = intdeque_new();
    int a = 1, b = 2, c = 3, d_val = 4;
    // [1]
    assert(intdeque_push_back(&d, &a));
    // [2, 1]
    assert(intdeque_push_front(&d, &b));
    // [2, 1, 3]
    assert(intdeque_push_back(&d, &c));
    // [4, 2, 1, 3]
    assert(intdeque_push_front(&d, &d_val));
    assert(intdeque_size(&d) == 4);

    int out;
    // pop front: 4
    assert(intdeque_pop_front(&d, &out));
    assert(out == 4);
    // pop back: 3
    assert(intdeque_pop_back(&d, &out));
    assert(out == 3);
    // pop front: 2
    assert(intdeque_pop_front(&d, &out));
    assert(out == 2);
    // pop front: 1
    assert(intdeque_pop_front(&d, &out));
    assert(out == 1);

    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_destroy_non_empty(void) {
    printf("Test: destroy on non-empty deque does not crash\n");
    intdeque_t d = intdeque_new();
    int v;
    for (int i = 0; i < 5; i++) {
        v = i;
        intdeque_push_back(&d, &v);
    }
    intdeque_destroy(&d);
    assert(intdeque_is_empty(&d));
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Builds a deque holding logical values [first, first+count) while forcing the
// internal buffer to start at physical offset @p start_offset, so that the stored
// range wraps around the end of the buffer when start_offset + count > SIZE.
static intdeque_t make_wrapped(int start_offset, int count, int first) {
    intdeque_t d = intdeque_new();
    // Advance the internal start by pushing placeholders at the back and draining
    // them from the front (pop_front advances _start without resetting it).
    for (int i = 0; i < start_offset; i++) {
        int tmp = -1;
        assert(intdeque_push_back(&d, &tmp));
    }
    for (int i = 0; i < start_offset; i++) {
        assert(intdeque_pop_front(&d, NULL));
    }
    assert(intdeque_is_empty(&d));
    for (int i = 0; i < count; i++) {
        int v = first + i;
        assert(intdeque_push_back(&d, &v));
    }
    return d;
}

// ── Tests: iteration ──────────────────────────────────────────────────────────

static void test_iter_empty(void) {
    printf("Test (iter): begin() == end() for empty deque\n");
    intdeque_t d = intdeque_new();
    assert(intdeque_begin(&d) == intdeque_end(&d));
    intdeque_destroy(&d);
}

static void test_iter_order(void) {
    printf("Test (iter): iterates front-to-back, including wrap-around\n");
    // start_offset = 5, count = 6 -> stored range wraps the 8-slot buffer.
    intdeque_t d = make_wrapped(5, 6, 100);
    int expected = 100;
    for (intdeque_iter_t it = intdeque_begin(&d); it != intdeque_end(&d); it = intdeque_iter_next(&d, it)) {
        assert(*intdeque_get(&d, it) == expected);
        expected++;
    }
    assert(expected == 106);
    intdeque_destroy(&d);
}

static void test_get_at_accessors(void) {
    printf("Test (iter): get/at/const_get/const_at return correct elements and bounds\n");
    intdeque_t d = make_wrapped(6, 5, 10);  // wraps: values 10..14
    const intdeque_t *cd = &d;
    for (size_t i = 0; i < 5; i++) {
        assert(*intdeque_at(&d, i) == (int)(10 + i));
        assert(*intdeque_const_at(cd, i) == (int)(10 + i));
        assert(*intdeque_get(&d, i) == (int)(10 + i));
        assert(*intdeque_const_get(cd, i) == (int)(10 + i));
    }
    // Out-of-bounds get/const_get return NULL.
    assert(intdeque_get(&d, 5) == NULL);
    assert(intdeque_const_get(cd, 5) == NULL);
    intdeque_destroy(&d);
}

// ── Tests: remove_at ──────────────────────────────────────────────────────────

static void test_remove_at_front(void) {
    printf("Test (remove_at): removing the front element preserves order\n");
    intdeque_t d = make_wrapped(6, 5, 0);  // 0 1 2 3 4 (wrapped)
    int out = -1;
    intdeque_iter_t next = 99;
    intdeque_remove_at(&d, 0, &out, &next);
    assert(out == 0);
    assert(next == 0);
    assert(intdeque_size(&d) == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(*intdeque_get(&d, i) == (int)(i + 1));
    }
    intdeque_destroy(&d);
}

static void test_remove_at_middle(void) {
    printf("Test (remove_at): removing a middle element preserves order and shifts\n");
    intdeque_t d = make_wrapped(6, 5, 0);  // 0 1 2 3 4 (wrapped)
    int out = -1;
    intdeque_iter_t next = 99;
    intdeque_remove_at(&d, 2, &out, &next);
    assert(out == 2);
    assert(next == 2);  // next element (former index 3) now sits at index 2
    assert(intdeque_size(&d) == 4);
    int expected[] = {0, 1, 3, 4};
    for (size_t i = 0; i < 4; i++) {
        assert(*intdeque_get(&d, i) == expected[i]);
    }
    intdeque_destroy(&d);
}

static void test_remove_at_last(void) {
    printf("Test (remove_at): removing the last element sets next to end()\n");
    intdeque_t d = make_wrapped(6, 5, 0);  // 0 1 2 3 4 (wrapped)
    int out = -1;
    intdeque_iter_t next = 99;
    intdeque_remove_at(&d, 4, &out, &next);
    assert(out == 4);
    assert(next == intdeque_end(&d));
    assert(intdeque_size(&d) == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(*intdeque_get(&d, i) == (int)i);
    }
    intdeque_destroy(&d);
}

static void test_remove_at_null_out(void) {
    printf("Test (remove_at): NULL out and NULL next_idx are accepted\n");
    intdeque_t d = make_wrapped(3, 4, 0);  // 0 1 2 3
    intdeque_remove_at(&d, 1, NULL, NULL);
    assert(intdeque_size(&d) == 3);
    int expected[] = {0, 2, 3};
    for (size_t i = 0; i < 3; i++) {
        assert(*intdeque_get(&d, i) == expected[i]);
    }
    intdeque_destroy(&d);
}

static void test_remove_at_single(void) {
    printf("Test (remove_at): single-element deque becomes empty\n");
    intdeque_t d = intdeque_new();
    int v = 42;
    assert(intdeque_push_back(&d, &v));
    int out = -1;
    intdeque_iter_t next = 99;
    intdeque_remove_at(&d, 0, &out, &next);
    assert(out == 42);
    assert(next == intdeque_end(&d));
    assert(intdeque_is_empty(&d));
    // After emptying, the deque can be reused normally.
    v = 7;
    assert(intdeque_push_back(&d, &v));
    assert(*intdeque_front(&d) == 7);
    intdeque_destroy(&d);
}

// ── Tests: swap_remove ────────────────────────────────────────────────────────

static void test_swap_remove_middle(void) {
    printf("Test (swap_remove): moves the back element into the removed slot\n");
    intdeque_t d = make_wrapped(6, 5, 0);  // 0 1 2 3 4 (wrapped)
    int out = -1;
    assert(intdeque_swap_remove(&d, 1, &out));
    assert(out == 1);
    assert(intdeque_size(&d) == 4);
    // Back element (4) moved into slot 1; order not preserved.
    int expected[] = {0, 4, 2, 3};
    for (size_t i = 0; i < 4; i++) {
        assert(*intdeque_get(&d, i) == expected[i]);
    }
    intdeque_destroy(&d);
}

static void test_swap_remove_last(void) {
    printf("Test (swap_remove): removing the last element leaves the rest intact\n");
    intdeque_t d = make_wrapped(6, 5, 0);  // 0 1 2 3 4 (wrapped)
    int out = -1;
    assert(intdeque_swap_remove(&d, 4, &out));
    assert(out == 4);
    assert(intdeque_size(&d) == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(*intdeque_get(&d, i) == (int)i);
    }
    intdeque_destroy(&d);
}

static void test_swap_remove_single(void) {
    printf("Test (swap_remove): single-element deque becomes empty\n");
    intdeque_t d = intdeque_new();
    int v = 5;
    assert(intdeque_push_back(&d, &v));
    int out = -1;
    assert(intdeque_swap_remove(&d, 0, &out));
    assert(out == 5);
    assert(intdeque_is_empty(&d));
    intdeque_destroy(&d);
}

static void test_swap_remove_out_of_bounds(void) {
    printf("Test (swap_remove): returns false when idx is out of bounds\n");
    intdeque_t d = intdeque_new();
    int out = -1;
    assert(!intdeque_swap_remove(&d, 0, &out));  // empty deque
    int v = 1;
    assert(intdeque_push_back(&d, &v));
    assert(!intdeque_swap_remove(&d, 1, &out));  // idx == size
    assert(intdeque_size(&d) == 1);
    intdeque_destroy(&d);
}

// ── Tests: algorithms_template.h macros ───────────────────────────────────────

static void test_foreach_macro(void) {
    printf("Test (_ZP_FOREACH): iterates every element of a wrapped deque\n");
    intdeque_t d = make_wrapped(5, 6, 1);  // 1 2 3 4 5 6 (wrapped)
    int sum = 0;
    int *elem = NULL;
    _ZP_FOREACH (intdeque, &d, elem) {
        sum += *elem;
    }
    assert(sum == 1 + 2 + 3 + 4 + 5 + 6);
    intdeque_destroy(&d);
}

static void test_const_foreach_macro(void) {
    printf("Test (_ZP_CONST_FOREACH): iterates a const-qualified deque\n");
    intdeque_t d = make_wrapped(5, 6, 1);
    const intdeque_t *cd = &d;
    int count = 0;
    const int *elem = NULL;
    _ZP_CONST_FOREACH (intdeque, cd, elem) {
        (void)elem;
        count++;
    }
    assert(count == 6);
    intdeque_destroy(&d);
}

static void test_find_macro(void) {
    printf("Test (_ZP_FIND): returns matching element or NULL\n");
    intdeque_t d = make_wrapped(5, 6, 1);  // 1 2 3 4 5 6
    int *found;
    _ZP_FIND(intdeque, &d, found, (*_ == 4));
    assert(found != NULL && *found == 4);
    _ZP_FIND(intdeque, &d, found, (*_ == 99));
    assert(found == NULL);
    intdeque_destroy(&d);
}

static void test_const_find_macro(void) {
    printf("Test (_ZP_CONST_FIND): locates an element in a const-qualified deque\n");
    intdeque_t d = make_wrapped(5, 6, 1);
    const intdeque_t *cd = &d;
    const int *found;
    _ZP_CONST_FIND(intdeque, cd, found, (*_ == 6));
    assert(found != NULL && *found == 6);
    _ZP_CONST_FIND(intdeque, cd, found, (*_ == 99));
    assert(found == NULL);
    intdeque_destroy(&d);
}

static void test_remove_all_macro(void) {
    printf("Test (_ZP_REMOVE_ALL): removes every matching element, preserving order\n");
    intdeque_t d = make_wrapped(5, 6, 1);  // 1 2 3 4 5 6 (wrapped)
    _ZP_REMOVE_ALL(intdeque, &d, (*_ % 2 == 0));
    assert(intdeque_size(&d) == 3);
    int expected[] = {1, 3, 5};
    for (size_t i = 0; i < 3; i++) {
        assert(*intdeque_get(&d, i) == expected[i]);
    }
    intdeque_destroy(&d);
}

static void test_remove_one_macro(void) {
    printf("Test (_ZP_REMOVE_ONE): removes only the first matching element\n");
    intdeque_t d = make_wrapped(3, 6, 0);  // 0 1 2 3 4 5
    _ZP_REMOVE_ONE(intdeque, &d, (*_ >= 2));
    assert(intdeque_size(&d) == 5);
    // Only value 2 removed; order preserved.
    int expected[] = {0, 1, 3, 4, 5};
    for (size_t i = 0; i < 5; i++) {
        assert(*intdeque_get(&d, i) == expected[i]);
    }
    intdeque_destroy(&d);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(void) {
    test_new_is_empty();
    test_push_back_pop_front();
    test_push_front_pop_front();
    test_push_back_pop_back();
    test_push_front_pop_back();
    test_front_back_peek();
    test_capacity_full();
    test_pop_empty();
    test_wrap_around_back();
    test_wrap_around_front();
    test_mixed_push_pop();
    test_destroy_non_empty();
    test_iter_empty();
    test_iter_order();
    test_get_at_accessors();
    test_remove_at_front();
    test_remove_at_middle();
    test_remove_at_last();
    test_remove_at_null_out();
    test_remove_at_single();
    test_swap_remove_middle();
    test_swap_remove_last();
    test_swap_remove_single();
    test_swap_remove_out_of_bounds();
    test_foreach_macro();
    test_const_foreach_macro();
    test_find_macro();
    test_const_find_macro();
    test_remove_all_macro();
    test_remove_one_macro();
    printf("All deque tests passed.\n");
    return 0;
}
