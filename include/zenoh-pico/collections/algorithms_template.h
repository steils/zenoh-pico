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
#ifndef INCLUDE_ZENOH_PICO_COLLECTIONS_ALGORITHMS_TEMPLATE_H
#define INCLUDE_ZENOH_PICO_COLLECTIONS_ALGORITHMS_TEMPLATE_H

#include <stdbool.h>

#include "zenoh-pico/collections/cat.h"

#define _ZP_TRANSFORM_IDENTITY(x) (x)
#define _ZP_TRANSFORM_VAL(x) (&(x)->val)

#define _ZP_FOREACH_TRANSFORM(collection_name, collection_ptr, var_name, transform)                   \
    for (collection_name##_iter_t _ZP_CAT(var_name, iter) = collection_name##_begin(collection_ptr);  \
         _ZP_CAT(var_name, iter) != collection_name##_end(collection_ptr) &&                          \
         (var_name = transform(collection_name##_at(collection_ptr, _ZP_CAT(var_name, iter))), true); \
         _ZP_CAT(var_name, iter) = collection_name##_iter_next(collection_ptr, _ZP_CAT(var_name, iter)))

#define _ZP_CONST_FOREACH_TRANSFORM(collection_name, collection_ptr, var_name, transform)                   \
    for (collection_name##_iter_t _ZP_CAT(var_name, iter) = collection_name##_begin(collection_ptr);        \
         _ZP_CAT(var_name, iter) != collection_name##_end(collection_ptr) &&                                \
         (var_name = transform(collection_name##_const_at(collection_ptr, _ZP_CAT(var_name, iter))), true); \
         _ZP_CAT(var_name, iter) = collection_name##_iter_next(collection_ptr, _ZP_CAT(var_name, iter)))

#define _ZP_FOREACH_FILTERED_TRANSFORM(collection_name, collection_ptr, var_name, transform, predicate)            \
    for (collection_name##_iter_t _ZP_CAT(var_name, iter) = collection_name##_begin(collection_ptr);               \
         _ZP_CAT(var_name, iter) != collection_name##_end(collection_ptr);                                         \
         _ZP_CAT(var_name, iter) = collection_name##_iter_next(collection_ptr, _ZP_CAT(var_name, iter)))           \
        if ((var_name = transform(collection_name##_at(collection_ptr, _ZP_CAT(var_name, iter))), !(predicate))) { \
        } else

#define _ZP_CONST_FOREACH_FILTERED_TRANSFORM(collection_name, collection_ptr, var_name, transform, predicate) \
    for (collection_name##_iter_t _ZP_CAT(var_name, iter) = collection_name##_begin(collection_ptr);          \
         _ZP_CAT(var_name, iter) != collection_name##_end(collection_ptr);                                    \
         _ZP_CAT(var_name, iter) = collection_name##_iter_next(collection_ptr, _ZP_CAT(var_name, iter)))      \
        if ((var_name = transform(collection_name##_const_at(collection_ptr, _ZP_CAT(var_name, iter))),       \
             !(predicate))) {                                                                                 \
        } else

// For loop over collection iterators. iter_name is a collection_name_iter_t variable which is automatically declared
// inside the loop.
#define _ZP_IT_FOREACH(collection_name, collection_ptr, iter_name)                     \
    for (collection_name##_iter_t iter_name = collection_name##_begin(collection_ptr); \
         iter_name != collection_name##_end(collection_ptr);                           \
         iter_name = collection_name##_iter_next(collection_ptr, iter_name))

// For loop over collection elements. var_name is a pointer to the element type which should be declared by user before
// the loop. In addition a collection_name_iter_t variable named var_name_iter is declared and used as the iterator.
#define _ZP_FOREACH(collection_name, collection_ptr, var_name) \
    _ZP_FOREACH_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_IDENTITY)
// For loop over const collection elements. var_name is a pointer to the element type which should be declared by user
// before the loop. In addition a collection_name_iter_t variable named var_name_iter is declared and used as the
// iterator.
#define _ZP_CONST_FOREACH(collection_name, collection_ptr, var_name) \
    _ZP_CONST_FOREACH_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_IDENTITY)

// For loop over collection elements matching predicate. The predicate is evaluated with var_name bound to the current
// element.
#define _ZP_FOREACH_FILTERED(collection_name, collection_ptr, var_name, predicate) \
    _ZP_FOREACH_FILTERED_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_IDENTITY, predicate)
// Const counterpart of _ZP_FOREACH_FILTERED
#define _ZP_CONST_FOREACH_FILTERED(collection_name, collection_ptr, var_name, predicate) \
    _ZP_CONST_FOREACH_FILTERED_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_IDENTITY, predicate)

// Find first element matching predicate.  var_name is a pointer to the element type which should be declared by
// user before the loop. It is set to NULL if no matching element is found
#define _ZP_FIND(collection_name, collection_ptr, var_name, predicate)                     \
    do {                                                                                   \
        var_name = NULL;                                                                   \
        for (collection_name##_iter_t _a_t_iter = collection_name##_begin(collection_ptr); \
             _a_t_iter != collection_name##_end(collection_ptr);                           \
             _a_t_iter = collection_name##_iter_next(collection_ptr, _a_t_iter)) {         \
            collection_name##_elem_t *_ = collection_name##_at(collection_ptr, _a_t_iter); \
            if (predicate) {                                                               \
                var_name = _;                                                              \
                break;                                                                     \
            }                                                                              \
        }                                                                                  \
    } while (0)

// Find first const element matching predicate.  var_name is a pointer to the element type which should be declared by
// user before the loop. It is set to NULL if no matching element is found
#define _ZP_CONST_FIND(collection_name, collection_ptr, var_name, predicate)                           \
    do {                                                                                               \
        var_name = NULL;                                                                               \
        for (collection_name##_iter_t _a_t_iter = collection_name##_begin(collection_ptr);             \
             _a_t_iter != collection_name##_end(collection_ptr);                                       \
             _a_t_iter = collection_name##_iter_next(collection_ptr, _a_t_iter)) {                     \
            const collection_name##_elem_t *_ = collection_name##_const_at(collection_ptr, _a_t_iter); \
            if (predicate) {                                                                           \
                var_name = _;                                                                          \
                break;                                                                                 \
            }                                                                                          \
        }                                                                                              \
    } while (0)

// Find first value matching predicate.  var_name is a pointer to the value type which should be declared by
// user before the loop. It is set to NULL if no matching element is found
#define _ZP_FIND_VAL(collection_name, collection_ptr, var_name, predicate)                      \
    do {                                                                                        \
        var_name = NULL;                                                                        \
        for (collection_name##_iter_t _a_t_iter = collection_name##_begin(collection_ptr);      \
             _a_t_iter != collection_name##_end(collection_ptr);                                \
             _a_t_iter = collection_name##_iter_next(collection_ptr, _a_t_iter)) {              \
            collection_name##_val_t *_ = &collection_name##_at(collection_ptr, _a_t_iter)->val; \
            if (predicate) {                                                                    \
                var_name = _;                                                                   \
                break;                                                                          \
            }                                                                                   \
        }                                                                                       \
    } while (0)

// Find first value matching predicate.  var_name is a pointer to the value type which should be declared by
// user before the loop. It is set to NULL if no matching element is found
#define _ZP_CONST_FIND_VAL(collection_name, collection_ptr, var_name, predicate)                            \
    do {                                                                                                    \
        var_name = NULL;                                                                                    \
        for (collection_name##_iter_t _a_t_iter = collection_name##_begin(collection_ptr);                  \
             _a_t_iter != collection_name##_end(collection_ptr);                                            \
             _a_t_iter = collection_name##_iter_next(collection_ptr, _a_t_iter)) {                          \
            const collection_name##_val_t *_ = &collection_name##_const_at(collection_ptr, _a_t_iter)->val; \
            if (predicate) {                                                                                \
                var_name = _;                                                                               \
                break;                                                                                      \
            }                                                                                               \
        }                                                                                                   \
    } while (0)

// Set begin iterator to point to the first element satisfying predicate within [begin, end) range.
// Will be set to end if no matching element is found.
#define _ZP_IT_FIND(collection_name, collection_ptr, begin, end, predicate)            \
    for (; begin != end; begin = collection_name##_iter_next(collection_ptr, begin)) { \
        collection_name##_elem_t *_ = collection_name##_at(collection_ptr, begin);     \
        (void)_;                                                                       \
        if (predicate) {                                                               \
            break;                                                                     \
        }                                                                              \
    }

// Set begin iterator to point to the first element satisfying predicate within [begin, end) range.
// Will be set to end if no matching element is found.
// Iterates over const collection.
#define _ZP_CONST_IT_FIND(collection_name, collection_ptr, begin, end, predicate)              \
    for (; begin != end; begin = collection_name##_iter_next(collection_ptr, begin)) {         \
        const collection_name##_elem_t *_ = collection_name##_const_at(collection_ptr, begin); \
        (void)_;                                                                               \
        if (predicate) {                                                                       \
            break;                                                                             \
        }                                                                                      \
    }

// For loop over hashmap values. var_name is a pointer to the value type which should be declared by user before
// the loop. In addition a collection_name_iter_t variable named var_name_iter is declared and used as the iterator.
#define _ZP_FOREACH_VAL(collection_name, collection_ptr, var_name) \
    _ZP_FOREACH_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_VAL)
// For loop over const hashmap values.  var_name is a pointer to the value type which should be declared by user before
// the loop. In addition a collection_name_iter_t variable named var_name_iter is declared and used as the iterator.
#define _ZP_CONST_FOREACH_VAL(collection_name, collection_ptr, var_name) \
    _ZP_CONST_FOREACH_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_VAL)

// For loop over hash-map values matching predicate. The predicate is evaluated with var_name bound to the current
// value.
#define _ZP_FOREACH_VAL_FILTERED(collection_name, collection_ptr, var_name, predicate) \
    _ZP_FOREACH_FILTERED_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_VAL, predicate)
// Const counterpart of _ZP_FOREACH_VAL_FILTERED.
#define _ZP_CONST_FOREACH_VAL_FILTERED(collection_name, collection_ptr, var_name, predicate) \
    _ZP_CONST_FOREACH_FILTERED_TRANSFORM(collection_name, collection_ptr, var_name, _ZP_TRANSFORM_VAL, predicate)

// Remove all elements matching predicate. Behaviour is undefined if predicate has side effects that modify the
// collection.
#define _ZP_REMOVE_ALL(collection_name, collection_ptr, predicate)                     \
    for (collection_name##_iter_t _a_t_iter = collection_name##_begin(collection_ptr); \
         _a_t_iter != collection_name##_end(collection_ptr);) {                        \
        collection_name##_elem_t *_ = collection_name##_at(collection_ptr, _a_t_iter); \
        (void)_;                                                                       \
        if (predicate) {                                                               \
            collection_name##_remove_at(collection_ptr, _a_t_iter, NULL, &_a_t_iter);  \
        } else {                                                                       \
            _a_t_iter = collection_name##_iter_next(collection_ptr, _a_t_iter);        \
        }                                                                              \
    }

// Remove at most one element matching predicate. Behaviour is undefined if predicate has side effects that modify the
// collection.
#define _ZP_REMOVE_ONE(collection_name, collection_ptr, predicate)                     \
    for (collection_name##_iter_t _a_t_iter = collection_name##_begin(collection_ptr); \
         _a_t_iter != collection_name##_end(collection_ptr);                           \
         _a_t_iter = collection_name##_iter_next(collection_ptr, _a_t_iter)) {         \
        collection_name##_elem_t *_ = collection_name##_at(collection_ptr, _a_t_iter); \
        (void)_;                                                                       \
        if (predicate) {                                                               \
            collection_name##_remove_at(collection_ptr, _a_t_iter, NULL, NULL);        \
            break;                                                                     \
        }                                                                              \
    }
#endif
