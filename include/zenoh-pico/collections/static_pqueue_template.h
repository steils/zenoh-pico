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

// user needs to define the following macros before including this file:
// _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE: the type of the elements in the priority queue
// _ZP_STATIC_PQUEUE_TEMPLATE_NAME: the name of the priority queue type to generate (without the _t suffix)
// _ZP_STATIC_PQUEUE_TEMPLATE_SIZE: the maximum size of the priority queue (optional, default is 16)
// _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN: the name of the function to destroy an element (optional, default is
// a no-op)
// _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN: the name of the function to move an element (optional, default
// is element-wise copy without destroying source)
// _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN: the name of the comparison function (elem_a, elem_b) -> int
//   should return <0 if a has higher priority than b, 0 if equal, >0 if b has higher priority than a
//   (i.e. min-priority queue by default: smallest element is at the top)
//
// Optional context support:
//   _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE: the type of an optional context passed to the compare function.
//       When defined, the compare macro signature becomes (elem_a, elem_b, ctx_ptr) where ctx_ptr is of type
//       _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE *.  The context is stored inside the queue struct and supplied to every
//       sift_up / sift_down call automatically.  Use new_with_ctx/init_with_context to initialise it; new()
//       zero-initialises it. When not defined (the default), the compare macro keeps its original (elem_a, elem_b)
//       signature and no context is stored.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "zenoh-pico/collections/cat.h"

#ifndef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE
#error "_ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE must be defined before including static_pqueue_template.h"
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE int
#endif
#ifndef _ZP_STATIC_PQUEUE_TEMPLATE_SIZE
#define _ZP_STATIC_PQUEUE_TEMPLATE_SIZE 16
#endif
#ifndef _ZP_STATIC_PQUEUE_TEMPLATE_NAME
#define _ZP_STATIC_PQUEUE_TEMPLATE_NAME \
    _ZP_CAT(_ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE, pqueue), _ZP_STATIC_PQUEUE_TEMPLATE_SIZE)
#endif
#ifndef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN
#error "_ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN must be defined before including static_pqueue_template.h"
#endif
#ifndef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN(x) (void)(x)
#endif
#ifndef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(dst, src) *(dst) = *(src);
#endif

// ── Context support ───────────────────────────────────────────────────────────
// Internally the template always calls _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL(a, b, ctx_ptr).
// When _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE is defined the user-supplied CMP_FN receives the pointer;
// otherwise we define the internal macro to call the 2-argument CMP_FN and ignore ctx.

#ifdef _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE
// Context-aware path: user compare macro is (elem_a, elem_b, ctx_ptr)
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL(a, b, pqueue) \
    _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN((a), (b), (pqueue->_cmp_ctx))
#else
// Context-free path: user compare macro is (elem_a, elem_b); ctx ignored
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL(a, b, pqueue) _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN((a), (b))
#endif

#define _ZP_STATIC_PQUEUE_TEMPLATE_TYPE _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, t)
typedef struct _ZP_STATIC_PQUEUE_TEMPLATE_TYPE {
    _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE _buffer[_ZP_STATIC_PQUEUE_TEMPLATE_SIZE];
    size_t _size;
#ifdef _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE
    _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE *_cmp_ctx;
#endif
} _ZP_STATIC_PQUEUE_TEMPLATE_TYPE;

// Type aliases required by algorithms_template.h macros (_ZP_FOREACH, _ZP_FIND, _ZP_REMOVE, ...).
typedef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, elem_t);
typedef size_t _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t);

// Input parameter type for push(). Elements are passed by const pointer when they
// are both trivially moveable (moved in via a plain copy that leaves the source
// intact) and trivially destructible, so push() never mutates or consumes the
// source. Otherwise a mutable pointer is required because a custom move may
// consume the source. The `const` is applied to the elem_t typedef rather than
// the underlying type so pointer element types keep the correct qualifier level.
#if defined(_ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE) && \
    defined(_ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE)
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_INPUT_TYPE const _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, elem_t)
#else
#define _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_INPUT_TYPE _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE
#endif

static inline _ZP_STATIC_PQUEUE_TEMPLATE_TYPE _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, new)(void) {
    _ZP_STATIC_PQUEUE_TEMPLATE_TYPE pqueue = {0};
    return pqueue;
}

static inline void _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, init)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    memset(pqueue, 0, sizeof(*pqueue));
}

#ifdef _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE
// new_with_ctx: initialise the queue and store a context pointer for comparisons.
static inline _ZP_STATIC_PQUEUE_TEMPLATE_TYPE _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME,
                                                      new_with_ctx)(_ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE *ctx) {
    _ZP_STATIC_PQUEUE_TEMPLATE_TYPE pqueue = {0};
    pqueue._cmp_ctx = ctx;
    return pqueue;
}

// init_with_ctx: initialise the queue and store a context pointer for comparisons.
static inline void _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME,
                           init_with_ctx)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                          _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE *ctx) {
    memset(pqueue, 0, sizeof(*pqueue));
    pqueue->_cmp_ctx = ctx;
}

// set_ctx: overwrite the context pointer in an existing queue.
// This is useful if the context needs to be updated after the queue is created (for example in case of move of
// self-referencing structs), or if new() was used to create a zero-initialised queue and the context pointer needs to
// be set later.
static inline void _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, set_ctx)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                     _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE *ctx) {
    pqueue->_cmp_ctx = ctx;
}
#endif

// Destroys all elements in the priority queue by calling the configured destroy function on each one,
// then resets the queue to an empty state. Does not free the queue struct itself.
static inline void _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, destroy)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    for (size_t i = 0; i < pqueue->_size; i++) {
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN(&pqueue->_buffer[i]);
    }
    pqueue->_size = 0;
}

// Returns the number of elements currently stored in the priority queue.
static inline size_t _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, size)(const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    return pqueue->_size;
}

// Returns true if the priority queue contains no elements.
static inline bool _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, is_empty)(const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    return pqueue->_size == 0;
}

// Return a pointer to the element at the top of the priority queue without removing it,
// or NULL if the queue is empty.
static inline _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME,
                                                            peek)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    if (pqueue->_size == 0) {
        return NULL;
    }
    return &pqueue->_buffer[0];
}

static inline size_t _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, sift_up)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                       size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (_ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL(&pqueue->_buffer[i], &pqueue->_buffer[parent], pqueue) < 0) {
            _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE tmp;
            _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&tmp, &pqueue->_buffer[parent]);
            _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[parent], &pqueue->_buffer[i]);
            _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[i], &tmp);
            i = parent;
        } else {
            break;
        }
    }
    return i;
}
static inline size_t _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, sift_down)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                         size_t i) {
    while (true) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t best = i;
        if (left < pqueue->_size &&
            _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL(&pqueue->_buffer[left], &pqueue->_buffer[best], pqueue) < 0) {
            best = left;
        }
        if (right < pqueue->_size &&
            _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL(&pqueue->_buffer[right], &pqueue->_buffer[best], pqueue) < 0) {
            best = right;
        }
        if (best == i) {
            break;
        }
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE tmp;
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&tmp, &pqueue->_buffer[i]);
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[i], &pqueue->_buffer[best]);
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[best], &tmp);
        i = best;
    }
    return i;
}

// Inserts an element into the priority queue.
static inline bool _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, push)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                  _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_INPUT_TYPE *elem) {
    if (pqueue->_size == _ZP_STATIC_PQUEUE_TEMPLATE_SIZE) {
        return false;
    }
    _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[pqueue->_size], elem);
    _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, sift_up)(pqueue, pqueue->_size);
    pqueue->_size++;
    return true;
}

// Removes the top element from the priority queue and moves it into @p out.
// @p out can be NULL, in which case the top element is destroyed in place.
// Returns false if the queue is empty, true otherwise.
static inline bool _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, pop)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                 _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *out) {
    if (pqueue->_size == 0) {
        return false;
    }
    if (out != NULL) {
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(out, &pqueue->_buffer[0]);
    } else {
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN(&pqueue->_buffer[0]);
    }
    pqueue->_size--;
    if (pqueue->_size > 0) {
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[0], &pqueue->_buffer[pqueue->_size]);
        _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, sift_down)(pqueue, 0);
    }
    return true;
}

// Removes the element at position @p idx and restores the heap property.
// The last element is moved into the vacated slot and the heap is re-heapified by calling
// sift_up followed by sift_down on the replacement: exactly one of the two does real work.
// If @p out is non-NULL the removed element is moved into it; otherwise it is destroyed in place.
// If @p next_idx is non-NULL it is set to the iterator of the next element to visit:
//   - If the replacement sifted UP to position k < idx, *next_idx is set to k so that a
//     forward iteration (e.g. _ZP_REMOVE) does not miss the newly placed element.
//   - Otherwise *next_idx is set to idx (the slot now contains a new element to check).
//   - When @p idx was the last slot, *next_idx is set to end().
// Returns false if @p idx is out of bounds, true otherwise.
static inline bool _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME,
                           remove_at)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                      _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) idx,
                                      _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *out,
                                      _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) * next_idx) {
    if (idx >= pqueue->_size) {
        return false;
    }
    if (out != NULL) {
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(out, &pqueue->_buffer[idx]);
    } else {
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN(&pqueue->_buffer[idx]);
    }
    pqueue->_size--;
    if (idx < pqueue->_size) {
        // Move the last element into the vacated slot and restore heap order.
        _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN(&pqueue->_buffer[idx], &pqueue->_buffer[pqueue->_size]);
        // sift_up returns the final position k of the replacement.
        // If k < idx the replacement landed at an already-visited slot; resume iteration
        // from k so _ZP_REMOVE does not miss it.  Otherwise resume from idx.
        size_t k = _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, sift_up)(pqueue, idx);
        _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, sift_down)(pqueue, idx);
        if (next_idx != NULL) {
            *next_idx = (k < idx) ? k : idx;
        }
    } else {
        if (next_idx != NULL) {
            *next_idx = pqueue->_size;
        }
    }
    return true;
}

// ── Iteration ─────────────────────────────────────────────────────────────────
// Iterates over the heap buffer in storage order (not priority order).
// Usage:
//   for (myqueue_iter_t i = myqueue_begin(&q); i != myqueue_end(&q); i = myqueue_iter_next(&q, i)) {
//       MyElem *e = myqueue_get(&q, i);
//   }

// Returns a pointer to the element at position @p idx in the underlying buffer.
// @p idx must satisfy idx < size().
static inline _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME,
                                                            at)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) idx) {
    return &pqueue->_buffer[idx];
}

// Returns a const pointer to the element at position @p idx in the underlying buffer.
// @p idx must satisfy idx < size().
static inline const _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, const_at)(
    const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue, _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) idx) {
    return &pqueue->_buffer[idx];
}

// Returns a pointer to the element at position @p idx in the underlying buffer, or NULL if idx >= size().
static inline _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME,
                                                            get)(_ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                                 _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) idx) {
    return idx < pqueue->_size ? &pqueue->_buffer[idx] : NULL;
}

// Returns a const pointer to the element at position @p idx in the underlying buffer, or NULL if idx >= size().
static inline const _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE *_ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, const_get)(
    const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue, _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) idx) {
    return idx < pqueue->_size ? &pqueue->_buffer[idx] : NULL;
}

// Returns the index of the first element (always 0).
static inline _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t)
    _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, begin)(const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    (void)pqueue;
    return 0;
}

// Returns the one-past-last index (equal to size()).
// Used as the end sentinel for iteration.
static inline _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t)
    _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, end)(const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue) {
    return pqueue->_size;
}

// Advances the iterator by one step.
static inline _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t)
    _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_next)(const _ZP_STATIC_PQUEUE_TEMPLATE_TYPE *pqueue,
                                                        _ZP_CAT(_ZP_STATIC_PQUEUE_TEMPLATE_NAME, iter_t) pos) {
    (void)pqueue;
    return pos + 1;
}

#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TYPE
#undef _ZP_STATIC_PQUEUE_TEMPLATE_NAME
#undef _ZP_STATIC_PQUEUE_TEMPLATE_SIZE
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_DESTROY_FN
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_MOVE_FN
#ifdef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_MOVEABLE
#endif
#ifdef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_TRIVIALLY_DESTRUCTIBLE
#endif
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_INPUT_TYPE
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_FN
#undef _ZP_STATIC_PQUEUE_TEMPLATE_TYPE
#ifdef _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE
#undef _ZP_STATIC_PQUEUE_TEMPLATE_CMP_CTX_TYPE
#endif
#undef _ZP_STATIC_PQUEUE_TEMPLATE_ELEM_CMP_INTERNAL
