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

// User needs to define the following macros before including this file:
// - _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE: the type of the elements in the deque (required)
// - _ZP_STATIC_DEQUE_TEMPLATE_NAME: the name of the deque type to generate, without the _t suffix
//   (optional, default is derived from the element type and size)
// - _ZP_STATIC_DEQUE_TEMPLATE_SIZE: the maximum size of the fixed-capacity circular buffer
//   stored in the generated struct (optional, default is 16)
// - _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN: the function-like macro used to destroy
//   an element (optional, default is a no-op)
// - _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN: the function-like macro used to move an
//   element (optional, default performs assignment without destroying the source element)

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "zenoh-pico/collections/cat.h"

#ifndef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE
#error "_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE must be defined before including static_deque_template.h"
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE int
#endif
#ifndef _ZP_STATIC_DEQUE_TEMPLATE_SIZE
#define _ZP_STATIC_DEQUE_TEMPLATE_SIZE 16
#endif
#ifndef _ZP_STATIC_DEQUE_TEMPLATE_NAME
#define _ZP_STATIC_DEQUE_TEMPLATE_NAME \
    _ZP_CAT(_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE, deque), _ZP_STATIC_DEQUE_TEMPLATE_SIZE)
#endif

#ifndef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN(x) (void)(x)
#endif
#ifndef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(dst, src) *(dst) = *(src);
#endif

#define _ZP_STATIC_DEQUE_TEMPLATE_TYPE _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, t)
typedef struct _ZP_STATIC_DEQUE_TEMPLATE_TYPE {
    _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE _buffer[_ZP_STATIC_DEQUE_TEMPLATE_SIZE];
    size_t _start;
    size_t _size;
} _ZP_STATIC_DEQUE_TEMPLATE_TYPE;

// Type aliases required by algorithms_template.h macros (_ZP_FOREACH, _ZP_FIND, _ZP_REMOVE, ...).
typedef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, elem_t);
typedef size_t _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t);

// Input parameter type for push_back/push_front. Elements are passed by const
// pointer when they are both trivially moveable (moved in via a plain copy that
// leaves the source intact) and trivially destructible, so these functions never
// mutate or consume the source. Otherwise a mutable pointer is required because a
// custom move may consume the source. The `const` is applied to the elem_t
// typedef rather than the underlying type so pointer element types keep the
// correct qualifier level.
#if defined(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE) && \
    defined(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE)
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_INPUT_TYPE const _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, elem_t)
#else
#define _ZP_STATIC_DEQUE_TEMPLATE_ELEM_INPUT_TYPE _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE
#endif

// Creates a new, empty deque. All fields are zero-initialised.
static inline _ZP_STATIC_DEQUE_TEMPLATE_TYPE _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, new)(void) {
    _ZP_STATIC_DEQUE_TEMPLATE_TYPE deque = {0};
    return deque;
}

// Returns the number of elements currently stored in the deque.
static inline size_t _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, size)(const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    return deque->_size;
}

// Destroys all elements in the deque by calling the configured destroy function on each one,
// then resets the deque to an empty state. Does not free the deque struct itself.
static inline void _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, destroy)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    for (size_t i = 0; i < deque->_size; i++) {
        size_t idx = deque->_start + i;
        if (idx >= _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
            idx -= _ZP_STATIC_DEQUE_TEMPLATE_SIZE;
        }
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN(&deque->_buffer[idx]);
    }
    deque->_start = 0;
    deque->_size = 0;
}

// Returns true if the deque contains no elements.
static inline bool _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, is_empty)(const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    return deque->_size == 0;
}

// Appends an element to the back of the deque by moving it from @p elem.
// Returns true on success, or false if the deque is at full capacity.
static inline bool _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, push_back)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                                      _ZP_STATIC_DEQUE_TEMPLATE_ELEM_INPUT_TYPE *elem) {
    if (deque->_size == _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
        return false;
    }
    size_t idx = deque->_start + deque->_size;
    if (idx >= _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
        idx -= _ZP_STATIC_DEQUE_TEMPLATE_SIZE;
    }
    _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(&deque->_buffer[idx], elem);
    deque->_size++;
    return true;
}

// Removes the element at the back of the deque.
// If @p out is non-NULL the element is moved into it; otherwise it is destroyed in place.
// Returns true on success, or false if the deque is empty.
static inline bool _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, pop_back)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                                     _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *out) {
    if (_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, is_empty)(deque)) {
        return false;
    }
    size_t idx = deque->_start + deque->_size - 1;
    if (idx >= _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
        idx -= _ZP_STATIC_DEQUE_TEMPLATE_SIZE;
    }
    if (out != NULL) {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(out, &deque->_buffer[idx]);
    } else {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN(&deque->_buffer[idx]);
    }
    if (deque->_size == 1) {
        deque->_start = 0;  // reset to initial state when empty
    }
    deque->_size--;
    return true;
}

// Returns a pointer to the element at the back of the deque without removing it,
// or NULL if the deque is empty.
static inline _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME,
                                                           back)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    if (_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, is_empty)(deque)) {
        return NULL;
    }
    size_t idx = deque->_start + deque->_size - 1;
    if (idx >= _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
        idx -= _ZP_STATIC_DEQUE_TEMPLATE_SIZE;
    }
    return &deque->_buffer[idx];
}

// Prepends an element to the front of the deque by moving it from @p elem.
// Returns true on success, or false if the deque is at full capacity.
static inline bool _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME,
                           push_front)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                       _ZP_STATIC_DEQUE_TEMPLATE_ELEM_INPUT_TYPE *elem) {
    if (_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, size)(deque) == _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
        return false;
    }
    if (deque->_start == 0) {
        deque->_start = _ZP_STATIC_DEQUE_TEMPLATE_SIZE;
    }
    deque->_start--;
    deque->_size++;
    _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(&deque->_buffer[deque->_start], elem);
    return true;
}

// Removes the element at the front of the deque.
// If @p out is non-NULL the element is moved into it; otherwise it is destroyed in place.
// Returns true on success, or false if the deque is empty.
static inline bool _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, pop_front)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                                      _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *out) {
    if (_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, is_empty)(deque)) {
        return false;
    }
    if (out != NULL) {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(out, &deque->_buffer[deque->_start]);
    } else {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN(&deque->_buffer[deque->_start]);
    }
    deque->_start++;
    if (deque->_start == _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
        deque->_start = 0;
    }
    deque->_size--;
    return true;
}

// Returns a pointer to the element at the front of the deque without removing it,
// or NULL if the deque is empty.
static inline _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME,
                                                           front)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    if (_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, is_empty)(deque)) {
        return NULL;
    }
    return &deque->_buffer[deque->_start];
}

// ── Iteration ─────────────────────────────────────────────────────────────────
// Iterates over the deque in front-to-back order.
// Usage:
//   for (mydeque_iter_t i = mydeque_begin(&d); i != mydeque_end(&d); i = mydeque_iter_next(&d, i)) {
//       MyElem *e = mydeque_get(&d, i);
//   }

// Maps a logical iterator position to the corresponding physical buffer index.
#define _ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)                \
    (((deque)->_start + (idx) >= _ZP_STATIC_DEQUE_TEMPLATE_SIZE)      \
         ? ((deque)->_start + (idx) - _ZP_STATIC_DEQUE_TEMPLATE_SIZE) \
         : ((deque)->_start + (idx)))

// Returns a pointer to the element at logical position @p idx.
// @p idx must satisfy idx < size().
static inline _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME,
                                                           at)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                               _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) idx) {
    return &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)];
}

// Returns a const pointer to the element at logical position @p idx.
// @p idx must satisfy idx < size().
static inline const _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, const_at)(
    const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque, _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) idx) {
    return &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)];
}

// Returns a pointer to the element at logical position @p idx, or NULL if idx >= size().
static inline _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME,
                                                           get)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                                _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) idx) {
    return idx < deque->_size ? &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)] : NULL;
}

// Returns a const pointer to the element at logical position @p idx, or NULL if idx >= size().
static inline const _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, const_get)(
    const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque, _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) idx) {
    return idx < deque->_size ? &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)] : NULL;
}

// Removes the element at logical position @p idx, shifting the remaining elements to close the gap
// and thereby preserving their relative order. To minimise work the shorter of the two sides
// (the elements before or after @p idx) is moved.
// This function mirrors the hashmap/vector remove_at signature so the deque can be used with the
// _ZP_REMOVE macros from algorithms_template.h.
// If @p out is non-NULL the removed element is moved into it; otherwise it is destroyed in place.
// If @p next_idx is non-NULL it is set to the iterator of the next element to visit: because the
// shift keeps the order, this is the same index when another element followed, or the end()
// iterator when the removed element was the last one.
// Behaviour is undefined if @p idx is out of bounds (idx >= size).
static inline void _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME,
                           remove_at)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                      _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) idx,
                                      _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *out,
                                      _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) * next_idx) {
    if (out != NULL) {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(out, &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)]);
    } else {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN(&deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx)]);
    }
    size_t phys_gap = _ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx);
    size_t back_count = deque->_size - 1 - idx;
    if (idx <= back_count) {
        // Fewer (or equal) elements in front: shift [0, idx) one slot toward the gap and drop the front.
#if defined(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE)
        if (phys_gap >= deque->_start) {
            // The front run [_start, phys_gap] does not wrap: shift it up by one slot in a single move.
            memmove(&deque->_buffer[deque->_start + 1], &deque->_buffer[deque->_start],
                    idx * sizeof(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE));
        } else
#endif
        {
            for (size_t j = idx; j > 0; j--) {
                _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(
                    &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, j)],
                    &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, j - 1)]);
            }
        }
        deque->_start++;
        if (deque->_start == _ZP_STATIC_DEQUE_TEMPLATE_SIZE) {
            deque->_start = 0;
        }
    } else {
        // Fewer elements behind: shift (idx, size) one slot toward the gap.
#if defined(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE)
        size_t phys_last = _ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, deque->_size - 1);
        if (phys_last >= phys_gap) {
            // The back run [phys_gap, phys_last] does not wrap: shift it down by one slot in a single move.
            memmove(&deque->_buffer[phys_gap], &deque->_buffer[phys_gap + 1],
                    back_count * sizeof(_ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE));
        } else
#endif
        {
            for (size_t j = idx; j < deque->_size - 1; j++) {
                _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(
                    &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, j)],
                    &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, j + 1)]);
            }
        }
    }
    deque->_size--;
    if (deque->_size == 0) {
        deque->_start = 0;  // reset to initial state when empty
    }
    if (next_idx != NULL) {
        // After the shift, idx addresses the element that followed the removed one, or equals
        // end() (== size) when the removed element was the last.
        *next_idx = idx;
    }
}

// Removes the element at logical position @p idx in O(1) by moving the back element into its place.
// This does NOT preserve the relative order of the remaining elements.
// If @p out is non-NULL the removed element is moved into it; otherwise it is destroyed in place.
// Returns true on success, or false if @p idx is out of bounds (idx >= size).
static inline bool _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, swap_remove)(_ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                                        _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t)
                                                                            idx,
                                                                        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE *out) {
    if (idx >= deque->_size) {
        return false;
    }
    size_t phys = _ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, idx);
    if (out != NULL) {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(out, &deque->_buffer[phys]);
    } else {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN(&deque->_buffer[phys]);
    }
    deque->_size--;
    // If the removed element was not the last, move the (former) back element into the vacated slot.
    if (idx != deque->_size) {
        _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN(
            &deque->_buffer[phys], &deque->_buffer[_ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX(deque, deque->_size)]);
    }
    if (deque->_size == 0) {
        deque->_start = 0;  // reset to initial state when empty
    }
    return true;
}

// Returns the logical index of the first element (always 0).
static inline _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t)
    _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, begin)(const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    (void)deque;
    return 0;
}

// Returns the one-past-last logical index (equal to size()).
// Used as the end sentinel for iteration.
static inline _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t)
    _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, end)(const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque) {
    return deque->_size;
}

// Advances the iterator by one step.
static inline _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t)
    _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_next)(const _ZP_STATIC_DEQUE_TEMPLATE_TYPE *deque,
                                                       _ZP_CAT(_ZP_STATIC_DEQUE_TEMPLATE_NAME, iter_t) pos) {
    (void)deque;
    return pos + 1;
}

#undef _ZP_STATIC_DEQUE_TEMPLATE_TYPE
#undef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TYPE
#undef _ZP_STATIC_DEQUE_TEMPLATE_NAME
#undef _ZP_STATIC_DEQUE_TEMPLATE_NODE_TYPE
#undef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_DESTROY_FN
#undef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_MOVE_FN
#ifdef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE
#undef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE
#endif
#ifdef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE
#undef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE
#endif
#undef _ZP_STATIC_DEQUE_TEMPLATE_ELEM_INPUT_TYPE
#undef _ZP_STATIC_DEQUE_TEMPLATE_SIZE
#undef _ZP_STATIC_DEQUE_TEMPLATE_PHYS_IDX
