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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zenoh-pico.h"

#if Z_FEATURE_ADMIN_SPACE == 1 && Z_FEATURE_UNICAST_PEER == 1

#include "zenoh-pico/collections/algorithms_template.h"
#include "zenoh-pico/collections/string.h"
#include "zenoh-pico/utils/hash.h"
#include "zenoh-pico/utils/json_encoder.h"
#include "zenoh-pico/utils/result.h"

#undef NDEBUG
#include <assert.h>

// ─────────────────────────────────────────────────────────────────────────────
// A minimal JSON deserializer for data produced by `_z_json_encoder_t`.
//
// It is built entirely on top of the generic collection templates:
//   • a `variant`  represents a JSON value (null / bool / number / string /
//     array / object);
//   • a `vector`   represents a JSON array (owning pointers to values);
//   • a `hashmap`  represents a JSON object (`z_owned_string_t` key -> owning
//     value pointer).
//
// Because a JSON value can contain arrays and objects, and those in turn contain
// values, the containers store *pointers* to `json_value_t`, which lets us break
// the type-recursion cycle: the value type is forward-declared, the containers
// are instantiated over `json_value_t *`, and finally the variant (which embeds
// the containers by value) is instantiated to complete the value type.
//
// Object keys are owning `z_owned_string_t` copies of the source content, so the
// deserialized value is self-contained and independent of the source document.
// ─────────────────────────────────────────────────────────────────────────────

// Forward declaration of the JSON value type completed by the variant template.
typedef struct json_value_t json_value_t;

// Recursively destroys a boxed (heap-allocated) JSON value. Defined after the
// variant is instantiated; forward-declared here so the container templates can
// reference it from their (static inline) element/value destructors.
static void _json_value_ptr_destroy(json_value_t **pp);

// ── JSON array: heap vector of owning `json_value_t *` ────────────────────────
#define _ZP_VECTOR_TEMPLATE_ELEM_TYPE json_value_t *
#define _ZP_VECTOR_TEMPLATE_NAME json_arr
#define _ZP_VECTOR_TEMPLATE_ELEM_DESTROY_FN(x) _json_value_ptr_destroy(x)
#define _ZP_VECTOR_TEMPLATE_ELEM_MOVE_FN(dst, src) (*(dst) = *(src), *(src) = NULL)
#define _ZP_VECTOR_TEMPLATE_ALLOC_FN z_malloc
#define _ZP_VECTOR_TEMPLATE_FREE_FN z_free
#include "zenoh-pico/collections/vector_template.h"

// ── JSON object: heap hashmap of owning `z_owned_string_t` -> owning `json_value_t *` ─
static inline size_t _json_key_hash(const z_owned_string_t *k) {
    return _z_fnv1_hash((const uint8_t *)_z_string_data(&k->_val), _z_string_len(&k->_val));
}

#define _ZP_HASHMAP_TEMPLATE_KEY_TYPE z_owned_string_t
#define _ZP_HASHMAP_TEMPLATE_VAL_TYPE json_value_t *
#define _ZP_HASHMAP_TEMPLATE_NAME json_obj
#define _ZP_HASHMAP_TEMPLATE_KEY_HASH_FN _json_key_hash
#define _ZP_HASHMAP_TEMPLATE_KEY_EQ_FN(a, b) _z_string_equals(&(a)->_val, &(b)->_val)
#define _ZP_HASHMAP_TEMPLATE_KEY_DESTROY_FN(k) _z_string_clear(&(k)->_val)
#define _ZP_HASHMAP_TEMPLATE_KEY_MOVE_FN(d, s) (*(d) = *(s), (s)->_val = _z_string_null())
#define _ZP_HASHMAP_TEMPLATE_VAL_DESTROY_FN(v) _json_value_ptr_destroy(v)
#define _ZP_HASHMAP_TEMPLATE_VAL_MOVE_FN(d, s) (*(d) = *(s), *(s) = NULL)
#define _ZP_HASHMAP_TEMPLATE_ALLOC_FN z_malloc
#define _ZP_HASHMAP_TEMPLATE_FREE_FN z_free
#include "zenoh-pico/collections/hashmap_template.h"

// ── JSON value: variant over the JSON alternatives ───────────────────────────
// The NONE state of the variant represents JSON `null`.
#define _ZP_VARIANT_TEMPLATE_NAME json_value
#define _ZP_VARIANT_TEMPLATE_1_TYPE bool
#define _ZP_VARIANT_TEMPLATE_1_NAME boolean
#define _ZP_VARIANT_TEMPLATE_2_TYPE double
#define _ZP_VARIANT_TEMPLATE_2_NAME number
#define _ZP_VARIANT_TEMPLATE_3_TYPE _z_string_t
#define _ZP_VARIANT_TEMPLATE_3_NAME string
#define _ZP_VARIANT_TEMPLATE_3_DESTROY_FN(ptr) _z_string_clear(ptr)
#define _ZP_VARIANT_TEMPLATE_3_MOVE_FN(dst, src) (*(dst) = *(src), *(src) = _z_string_null())
#define _ZP_VARIANT_TEMPLATE_4_TYPE json_arr_t
#define _ZP_VARIANT_TEMPLATE_4_NAME array
#define _ZP_VARIANT_TEMPLATE_4_DESTROY_FN(ptr) json_arr_destroy(ptr)
#define _ZP_VARIANT_TEMPLATE_4_MOVE_FN(dst, src) (*(dst) = *(src), json_arr_init(src))
#define _ZP_VARIANT_TEMPLATE_5_TYPE json_obj_t
#define _ZP_VARIANT_TEMPLATE_5_NAME object
#define _ZP_VARIANT_TEMPLATE_5_DESTROY_FN(ptr) json_obj_destroy(ptr)
#define _ZP_VARIANT_TEMPLATE_5_MOVE_FN(dst, src) (*(dst) = *(src), json_obj_init(src))
#include "zenoh-pico/collections/variant_template.h"

static void _json_value_ptr_destroy(json_value_t **pp) {
    if (pp != NULL && *pp != NULL) {
        json_value_destroy(*pp);
        z_free(*pp);
        *pp = NULL;
    }
}

// Moves `*v` onto a freshly heap-allocated value, leaving `*v` empty (null).
static json_value_t *_json_box(json_value_t *v) {
    json_value_t *p = (json_value_t *)z_malloc(sizeof(json_value_t));
    if (p == NULL) {
        return NULL;
    }
    *p = *v;
    *v = json_value_none();
    return p;
}

// ── Parser ───────────────────────────────────────────────────────────────────

typedef struct {
    const char *cur;
    const char *end;
} _json_parser_t;

static z_result_t _json_parse_value(_json_parser_t *p, json_value_t *out);

static void _json_skip_ws(_json_parser_t *p) {
    while (p->cur < p->end) {
        char c = *p->cur;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->cur++;
        } else {
            break;
        }
    }
}

static int _json_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Encodes a Unicode code point (BMP) as UTF-8 into `dst`, returning the number
// of bytes written (1..3).
static size_t _json_utf8_encode(uint32_t cp, char *dst) {
    if (cp < 0x80) {
        dst[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
}

// Parses a JSON string (the cursor must be on the opening quote) into `out`.
static z_result_t _json_parse_string(_json_parser_t *p, _z_string_t *out) {
    if (p->cur >= p->end || *p->cur != '"') {
        return _Z_ERR_INVALID;
    }
    const char *content = p->cur + 1;

    // First pass: find the closing quote (respecting escapes) to bound the buffer.
    const char *scan = content;
    while (scan < p->end && *scan != '"') {
        if (*scan == '\\') {
            scan++;
            if (scan >= p->end) {
                return _Z_ERR_INVALID;
            }
        }
        scan++;
    }
    if (scan >= p->end) {
        return _Z_ERR_INVALID;  // unterminated string
    }
    const char *close = scan;

    // The decoded content is never longer than the raw span.
    size_t raw_len = (size_t)(close - content);
    char *buf = (char *)z_malloc(raw_len + 1);
    if (buf == NULL) {
        return _Z_ERR_SYSTEM_OUT_OF_MEMORY;
    }

    size_t out_len = 0;
    const char *c = content;
    z_result_t res = Z_OK;
    while (c < close) {
        if (*c != '\\') {
            buf[out_len++] = *c++;
            continue;
        }
        c++;  // consume backslash
        if (c >= close) {
            res = _Z_ERR_INVALID;
            break;
        }
        switch (*c) {
            case '"':
                buf[out_len++] = '"';
                break;
            case '\\':
                buf[out_len++] = '\\';
                break;
            case '/':
                buf[out_len++] = '/';
                break;
            case 'b':
                buf[out_len++] = '\b';
                break;
            case 'f':
                buf[out_len++] = '\f';
                break;
            case 'n':
                buf[out_len++] = '\n';
                break;
            case 'r':
                buf[out_len++] = '\r';
                break;
            case 't':
                buf[out_len++] = '\t';
                break;
            case 'u': {
                if (close - c < 5) {
                    res = _Z_ERR_INVALID;
                    break;
                }
                uint32_t cp = 0;
                for (int i = 1; i <= 4; i++) {
                    int d = _json_hex_digit(c[i]);
                    if (d < 0) {
                        res = _Z_ERR_INVALID;
                        break;
                    }
                    cp = (cp << 4) | (uint32_t)d;
                }
                if (res != Z_OK) {
                    break;
                }
                out_len += _json_utf8_encode(cp, buf + out_len);
                c += 4;  // skip the 4 hex digits (the ++ below skips 'u')
                break;
            }
            default:
                res = _Z_ERR_INVALID;
                break;
        }
        if (res != Z_OK) {
            break;
        }
        c++;
    }

    if (res != Z_OK) {
        z_free(buf);
        return res;
    }

    *out = _z_string_copy_from_substr(buf, out_len);
    z_free(buf);

    p->cur = close + 1;  // move past the closing quote
    return Z_OK;
}

// Scans a JSON string token (cursor must be on the opening quote) and yields the
// raw (still-escaped) content span, advancing the cursor past the closing quote.
// Used for object keys, which are stored as non-owning views.
static z_result_t _json_scan_string_raw(_json_parser_t *p, const char **start, size_t *len) {
    if (p->cur >= p->end || *p->cur != '"') {
        return _Z_ERR_INVALID;
    }
    const char *content = p->cur + 1;

    const char *scan = content;
    while (scan < p->end && *scan != '"') {
        if (*scan == '\\') {
            scan++;
            if (scan >= p->end) {
                return _Z_ERR_INVALID;
            }
        }
        scan++;
    }
    if (scan >= p->end) {
        return _Z_ERR_INVALID;  // unterminated string
    }

    *start = content;
    *len = (size_t)(scan - content);
    p->cur = scan + 1;  // move past the closing quote
    return Z_OK;
}

static z_result_t _json_parse_number(_json_parser_t *p, double *out) {
    const char *start = p->cur;
    while (p->cur < p->end) {
        char c = *p->cur;
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
            p->cur++;
        } else {
            break;
        }
    }
    size_t len = (size_t)(p->cur - start);
    if (len == 0 || len >= 64) {
        return _Z_ERR_INVALID;
    }

    char buf[64] = {0};
    memcpy(buf, start, len);

    char *endptr = NULL;
    double v = strtod(buf, &endptr);
    if (endptr != buf + len) {
        return _Z_ERR_INVALID;
    }
    *out = v;
    return Z_OK;
}

static z_result_t _json_match_literal(_json_parser_t *p, const char *lit) {
    size_t len = strlen(lit);
    if ((size_t)(p->end - p->cur) < len || memcmp(p->cur, lit, len) != 0) {
        return _Z_ERR_INVALID;
    }
    p->cur += len;
    return Z_OK;
}

static z_result_t _json_parse_array(_json_parser_t *p, json_value_t *out) {
    p->cur++;  // consume '['
    json_arr_t arr;
    json_arr_init(&arr);

    _json_skip_ws(p);
    if (p->cur < p->end && *p->cur == ']') {
        p->cur++;
        *out = json_value_from_array(&arr);
        return Z_OK;
    }

    z_result_t res = Z_OK;
    while (true) {
        json_value_t val = json_value_none();
        res = _json_parse_value(p, &val);
        if (res != Z_OK) {
            break;
        }

        json_value_t *boxed = _json_box(&val);
        if (boxed == NULL) {
            json_value_destroy(&val);
            res = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
            break;
        }
        if (!json_arr_push_back(&arr, &boxed)) {
            _json_value_ptr_destroy(&boxed);
            res = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
            break;
        }

        _json_skip_ws(p);
        if (p->cur >= p->end) {
            res = _Z_ERR_INVALID;
            break;
        }
        if (*p->cur == ',') {
            p->cur++;
            continue;
        }
        if (*p->cur == ']') {
            p->cur++;
            break;
        }
        res = _Z_ERR_INVALID;
        break;
    }

    if (res != Z_OK) {
        json_arr_destroy(&arr);
        return res;
    }
    *out = json_value_from_array(&arr);
    return Z_OK;
}

static z_result_t _json_parse_object(_json_parser_t *p, json_value_t *out) {
    p->cur++;  // consume '{'
    json_obj_t map;
    json_obj_init(&map);

    _json_skip_ws(p);
    if (p->cur < p->end && *p->cur == '}') {
        p->cur++;
        *out = json_value_from_object(&map);
        return Z_OK;
    }

    z_result_t res = Z_OK;
    while (true) {
        _json_skip_ws(p);

        // Keys are owning copies of the source content. Admin-space keys are
        // simple identifiers, so the raw content span is copied verbatim.
        const char *key_start = NULL;
        size_t key_len = 0;
        res = _json_scan_string_raw(p, &key_start, &key_len);
        if (res != Z_OK) {
            break;
        }
        z_owned_string_t key;
        key._val = _z_string_copy_from_substr(key_start, key_len);

        _json_skip_ws(p);
        if (p->cur >= p->end || *p->cur != ':') {
            res = _Z_ERR_INVALID;
            break;
        }
        p->cur++;  // consume ':'

        json_value_t val = json_value_none();
        res = _json_parse_value(p, &val);
        if (res != Z_OK) {
            break;
        }

        json_value_t *boxed = _json_box(&val);
        if (boxed == NULL) {
            json_value_destroy(&val);
            res = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
            break;
        }

        json_obj_iter_t it = json_obj_insert(&map, &key, &boxed);
        if (it == json_obj_end(&map)) {
            _json_value_ptr_destroy(&boxed);
            _z_string_clear(&key._val);
            res = _Z_ERR_SYSTEM_OUT_OF_MEMORY;
            break;
        }

        _json_skip_ws(p);
        if (p->cur >= p->end) {
            res = _Z_ERR_INVALID;
            break;
        }
        if (*p->cur == ',') {
            p->cur++;
            continue;
        }
        if (*p->cur == '}') {
            p->cur++;
            break;
        }
        res = _Z_ERR_INVALID;
        break;
    }

    if (res != Z_OK) {
        json_obj_destroy(&map);
        return res;
    }
    *out = json_value_from_object(&map);
    return Z_OK;
}

static z_result_t _json_parse_value(_json_parser_t *p, json_value_t *out) {
    _json_skip_ws(p);
    if (p->cur >= p->end) {
        return _Z_ERR_INVALID;
    }

    char c = *p->cur;
    switch (c) {
        case '{':
            return _json_parse_object(p, out);
        case '[':
            return _json_parse_array(p, out);
        case '"': {
            _z_string_t s = _z_string_null();
            _Z_RETURN_IF_ERR(_json_parse_string(p, &s));
            *out = json_value_from_string(&s);
            return Z_OK;
        }
        case 't': {
            _Z_RETURN_IF_ERR(_json_match_literal(p, "true"));
            bool b = true;
            *out = json_value_from_boolean(&b);
            return Z_OK;
        }
        case 'f': {
            _Z_RETURN_IF_ERR(_json_match_literal(p, "false"));
            bool b = false;
            *out = json_value_from_boolean(&b);
            return Z_OK;
        }
        case 'n':
            _Z_RETURN_IF_ERR(_json_match_literal(p, "null"));
            *out = json_value_none();  // NONE state == JSON null
            return Z_OK;
        default: {
            double d = 0.0;
            _Z_RETURN_IF_ERR(_json_parse_number(p, &d));
            *out = json_value_from_number(&d);
            return Z_OK;
        }
    }
}

// Deserializes the JSON document held by `s` into `out`.
static z_result_t json_deserialize(const z_loaned_string_t *s, json_value_t *out) {
    _json_parser_t p = {.cur = z_string_data(s), .end = z_string_data(s) + z_string_len(s)};

    _Z_RETURN_IF_ERR(_json_parse_value(&p, out));

    _json_skip_ws(&p);
    if (p.cur != p.end) {
        json_value_destroy(out);  // trailing garbage
        *out = json_value_none();
        return _Z_ERR_INVALID;
    }
    return Z_OK;
}

void get_admin_space_ke(const z_loaned_session_t *zs, char *buf, size_t len) {
    memset(buf, 0, len);
    const z_id_t id = z_info_zid(zs);
    snprintf(buf, len,
             "@/"_Z_ID_PRINT_FORMAT
             "/peer",
             _Z_ID_PRINT_ARGS(&id));
}

z_result_t make_admin_space_query(const z_loaned_session_t *zs, json_value_t *out) {
    z_owned_closure_reply_t reply_callback;
    z_owned_fifo_handler_reply_t reply_handler;
    assert(z_fifo_channel_reply_new(&reply_callback, &reply_handler, 16) == Z_OK);

    char buf[64] = {0};
    get_admin_space_ke(zs, buf, sizeof(buf));
    z_view_keyexpr_t ke;
    z_view_keyexpr_from_substr_unchecked(&ke, buf, strlen(buf));
    z_get_options_t options;
    z_get_options_default(&options);
    options.consolidation = z_query_consolidation_none();
    assert(z_get(zs, z_loan(ke), "", z_move(reply_callback), &options) == Z_OK);

    z_owned_reply_t reply;
    z_result_t ret = z_try_recv(z_loan(reply_handler), &reply);
    if (ret == Z_OK) {
        assert(z_reply_is_ok(z_loan(reply)));
        z_owned_string_t json_str;
        assert(z_bytes_to_string(z_sample_payload(z_reply_ok(z_loan(reply))), &json_str) == Z_OK);
        assert(json_deserialize(z_loan(json_str), out) == Z_OK);
        z_string_drop(z_move(json_str));
        z_reply_drop(z_move(reply));
    }
    z_fifo_handler_reply_drop(z_move(reply_handler));
    return ret;
}

z_result_t create_listen_session(z_owned_session_t *s, const char *locator, bool auto_start_admin_space) {
    z_owned_config_t c;
    z_config_default(&c);
    zp_config_insert(z_loan_mut(c), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(c), Z_CONFIG_LISTEN_KEY, locator);
    zp_config_insert(z_loan_mut(c), Z_CONFIG_MULTICAST_SCOUTING_KEY, "false");
    if (auto_start_admin_space) {
        z_open_options_t options;
        z_open_options_default(&options);
        options.auto_start_admin_space = true;
        return z_open(s, z_move(c), &options);
    }
    return z_open(s, z_move(c), NULL);
}

z_result_t create_connect_session(z_owned_session_t *s, const char *locator) {
    z_owned_config_t c;
    z_config_default(&c);
    zp_config_insert(z_loan_mut(c), Z_CONFIG_MODE_KEY, "peer");
    zp_config_insert(z_loan_mut(c), Z_CONFIG_CONNECT_KEY, locator);
    zp_config_insert(z_loan_mut(c), Z_CONFIG_MULTICAST_SCOUTING_KEY, "false");
    return z_open(s, z_move(c), NULL);
}

void test_start_stop_admin_space(void) {
    printf("test_start_stop_admin_space\n");
    z_owned_session_t s;
    assert(create_listen_session(&s, "tcp/127.0.0.1:10000", false) == Z_OK);

    // admin space stopped - no response
    json_value_t out = json_value_none();
    assert(make_admin_space_query(z_loan(s), &out) == Z_CHANNEL_DISCONNECTED);

    // start admin space - get response
    assert(zp_start_admin_space(z_loan_mut(s)) == Z_OK);
    assert(make_admin_space_query(z_loan(s), &out) == Z_OK);
    json_value_destroy(&out);

    // admin space stopped - no response
    assert(zp_stop_admin_space(z_loan_mut(s)) == Z_OK);
    assert(make_admin_space_query(z_loan(s), &out) == Z_CHANNEL_DISCONNECTED);
    json_value_destroy(&out);

    z_drop(z_move(s));
}

void test_auto_start_admin_space(void) {
    printf("test_auto_start_admin_space\n");
    z_owned_session_t s;
    assert(create_listen_session(&s, "tcp/127.0.0.1:10001", true) == Z_OK);

    // admin space started automatically - receive response
    json_value_t out = json_value_none();
    assert(make_admin_space_query(z_loan(s), &out) == Z_OK);
    json_value_destroy(&out);
    z_drop(z_move(s));
}

json_value_t *json_obj_get_value(json_obj_t *o, const char *key) {
    z_owned_string_t k;
    k._val = _z_string_alias_str(key);
    json_value_t **res = json_obj_get(o, &k);
    return res != NULL ? *res : NULL;
}

#define _ZP_VECTOR_TEMPLATE_ELEM_TYPE const z_loaned_session_t *
#define _ZP_VECTOR_TEMPLATE_NAME z_loaned_session_vec
#include "zenoh-pico/collections/vector_template.h"

bool session_has_zid(const z_loaned_session_t *zs, const _z_string_t *zid_str) {
    z_id_t id = z_info_zid(zs);
    z_owned_string_t zid_str2;
    if (z_id_to_string(&id, &zid_str2) != Z_OK) {
        return false;
    }
    bool res = _z_string_equals(zid_str, z_loan(zid_str2));
    z_string_drop(z_move(zid_str2));
    return res;
}

void admin_space_verify(json_value_t *json, const z_loaned_session_t *zs, const char *listen_locator,
                        z_loaned_session_vec_t *connected_sessions) {
    char buf[64] = {0};
    assert(json_value_is_object(json));
    json_obj_t *obj = json_value_get_object(json);
    json_value_t *version = json_obj_get_value(obj, "version");
    assert(version != NULL);
    assert(json_value_is_string(version));
    const _z_string_t *version_str = json_value_get_string(version);
    assert(memcmp(ZENOH_PICO_VERSION, _z_string_data(version_str), _z_string_len(version_str)) == 0);

    memset(buf, 0, sizeof(buf));
    json_value_t *zid = json_obj_get_value(obj, "zid");
    assert(zid != NULL);
    assert(json_value_is_string(zid));
    const _z_string_t *zid_str = json_value_get_string(zid);
    z_id_t expected_zid = z_info_zid(zs);
    snprintf(buf, sizeof(buf), _Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&expected_zid));
    assert(memcmp(buf, _z_string_data(zid_str), _z_string_len(zid_str)) == 0);

    json_value_t *plugins = json_obj_get_value(obj, "plugins");
    assert(plugins != NULL);
    assert(json_value_is_object(plugins));
    assert(json_obj_size(json_value_get_object(plugins)) == 0);

    json_value_t *metadata = json_obj_get_value(obj, "metadata");
    assert(metadata != NULL);
    assert(json_value_is_none(metadata));

    json_value_t *locators = json_obj_get_value(obj, "locators");
    assert(locators != NULL);
    assert(json_value_is_array(locators));

    json_arr_t *locators_arr = json_value_get_array(locators);
    assert(json_arr_size(locators_arr) == 1);
    json_value_t *json_locator = *json_arr_at(locators_arr, 0);
    assert(json_value_is_string(json_locator));
    const _z_string_t *locator_str = json_value_get_string(json_locator);
    assert(memcmp(listen_locator, _z_string_data(locator_str), _z_string_len(locator_str)) == 0);

    json_value_t *sessions = json_obj_get_value(obj, "sessions");
    assert(sessions != NULL);
    assert(json_value_is_array(sessions));
    json_arr_t *sessions_arr = json_value_get_array(sessions);
    assert(json_arr_size(sessions_arr) == z_loaned_session_vec_size(connected_sessions));

    for (size_t i = 0; i < json_arr_size(sessions_arr); i++) {
        json_value_t *session = *json_arr_at(sessions_arr, i);
        assert(json_value_is_object(session));
        json_obj_t *session_obj = json_value_get_object(session);

        json_value_t *peer_zid = json_obj_get_value(session_obj, "peer");
        assert(peer_zid != NULL);
        assert(json_value_is_string(peer_zid));
        const _z_string_t *peer_zid_str = json_value_get_string(peer_zid);
        assert(_z_string_len(peer_zid_str) == Z_ZID_LENGTH * 2);

        z_loaned_session_vec_iter_t it = z_loaned_session_vec_begin(connected_sessions);
        z_loaned_session_vec_iter_t end = z_loaned_session_vec_end(connected_sessions);
        _ZP_IT_FIND(z_loaned_session_vec, connected_sessions, it, end, session_has_zid(*_, peer_zid_str));
        assert(it != end);

        json_value_t *whatami = json_obj_get_value(session_obj, "whatami");
        assert(whatami != NULL);
        assert(json_value_is_string(whatami));
        const _z_string_t *whatami_str = json_value_get_string(whatami);
        z_view_string_t session_whatami;
        assert(z_whatami_to_view_string((_Z_RC_IN_VAL(*z_loaned_session_vec_at(connected_sessions, it)))->_mode,
                                        &session_whatami) == Z_OK);
        assert(_z_string_equals(whatami_str, z_view_string_loan(&session_whatami)));

        z_loaned_session_vec_remove_at(connected_sessions, it, NULL, NULL);

        json_value_t *is_shm = json_obj_get_value(session_obj, "shm");
        assert(is_shm != NULL);
        assert(json_value_is_boolean(is_shm));
        assert(*json_value_get_boolean(is_shm) == false);

        json_value_t *weight = json_obj_get_value(session_obj, "weight");
        assert(weight != NULL);
        assert(json_value_is_none(weight));

        json_value_t *links = json_obj_get_value(session_obj, "links");
        assert(links != NULL);
        assert(json_value_is_array(links));
        json_arr_t *links_arr = json_value_get_array(links);
        assert(json_arr_size(links_arr) == 1);
        json_value_t *link = *json_arr_at(links_arr, 0);
        assert(json_value_is_object(link));
        json_obj_t *link_obj = json_value_get_object(link);
        json_value_t *link_src = json_obj_get_value(link_obj, "src");
        assert(link_src != NULL);
        assert(json_value_is_string(link_src));
        const _z_string_t *link_src_str = json_value_get_string(link_src);
        assert(memcmp(_z_string_data(link_src_str), listen_locator, _z_string_len(link_src_str)) == 0);
        json_value_t *link_dst = json_obj_get_value(link_obj, "dst");
        assert(link_dst != NULL);
        assert(json_value_is_string(link_dst));
    }
}

void test_admin_space_no_connections(void) {
    const char *listen_locator = "tcp/127.0.0.1:10002";
    printf("test_admin_space_no_connections\n");

    z_owned_session_t s;
    assert(create_listen_session(&s, listen_locator, true) == Z_OK);
    json_value_t json = json_value_none();
    assert(make_admin_space_query(z_loan(s), &json) == Z_OK);

    z_loaned_session_vec_t connected_sessions = z_loaned_session_vec_new();
    admin_space_verify(&json, z_loan(s), listen_locator, &connected_sessions);
    z_loaned_session_vec_destroy(&connected_sessions);

    json_value_destroy(&json);
    z_drop(z_move(s));
}

void test_admin_space_with_connections(void) {
    const char *listen_locator = "tcp/127.0.0.1:10003";
    printf("test_admin_space_with_connections\n");

    z_owned_session_t s;
    assert(create_listen_session(&s, listen_locator, true) == Z_OK);
    z_owned_session_t s1, s2;
    assert(create_connect_session(&s1, listen_locator) == Z_OK);
    assert(create_connect_session(&s2, listen_locator) == Z_OK);
    z_sleep_ms(1000);

    json_value_t json = json_value_none();
    assert(make_admin_space_query(z_loan(s), &json) == Z_OK);
    z_loaned_session_vec_t connected_sessions = z_loaned_session_vec_new();
    const z_loaned_session_t *s1l = z_loan(s1);
    const z_loaned_session_t *s2l = z_loan(s2);
    z_loaned_session_vec_push_back(&connected_sessions, &s1l);
    z_loaned_session_vec_push_back(&connected_sessions, &s2l);
    admin_space_verify(&json, z_loan(s), listen_locator, &connected_sessions);
    z_loaned_session_vec_destroy(&connected_sessions);

    json_value_destroy(&json);
    z_drop(z_move(s1));
    z_drop(z_move(s2));
    z_drop(z_move(s));
}

#if Z_FEATURE_CONNECTIVITY == 1 && Z_FEATURE_LOCAL_SUBSCRIBER == 1

z_result_t declare_local_subscriber(const z_loaned_session_t *s, const char *ke_str,
                                    z_owned_fifo_handler_sample_t *sub_handler) {
    z_subscriber_options_t sub_opts;
    z_subscriber_options_default(&sub_opts);
    sub_opts.allowed_origin = Z_LOCALITY_SESSION_LOCAL;

    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str_unchecked(&ke, ke_str);

    z_owned_closure_sample_t sub_callback;

    assert(z_fifo_channel_sample_new(&sub_callback, sub_handler, 16) == Z_OK);
    return z_declare_background_subscriber(s, z_loan(ke), z_move(sub_callback), &sub_opts);
}

z_result_t make_session_get(const z_loaned_session_t *s, const char *ke_str,
                            z_owned_fifo_handler_reply_t *reply_handler) {
    z_owned_closure_reply_t reply_callback;
    assert(z_fifo_channel_reply_new(&reply_callback, reply_handler, 16) == Z_OK);

    z_get_options_t get_opts;
    z_get_options_default(&get_opts);
    get_opts.consolidation = z_query_consolidation_none();
    get_opts.allowed_destination = Z_LOCALITY_SESSION_LOCAL;

    z_view_keyexpr_t ke;
    z_view_keyexpr_from_str_unchecked(&ke, ke_str);

    return z_get(s, z_loan(ke), "", z_move(reply_callback), &get_opts);
}

void verify_transport_ke(const z_loaned_sample_t *sample, const z_id_t *own_zid, const z_id_t *expected_zid) {
    char expected_ke[128] = {0};
    snprintf(expected_ke, sizeof(expected_ke),
             "@/"_Z_ID_PRINT_FORMAT
             "/session/transport/unicast/"_Z_ID_PRINT_FORMAT,
             _Z_ID_PRINT_ARGS(own_zid), _Z_ID_PRINT_ARGS(expected_zid));
    z_view_string_t actual_ke;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &actual_ke);
    assert(memcmp(expected_ke, _z_string_data(z_loan(actual_ke)), _z_string_len(z_loan(actual_ke))) == 0);
}
void verify_transport_delete(const z_loaned_sample_t *sample, const z_id_t *own_zid, const z_id_t *expected_zid,
                             z_whatami_t expected_whatami) {
    _ZP_UNUSED(expected_whatami);
    assert(z_sample_kind(sample) == Z_SAMPLE_KIND_DELETE);
    verify_transport_ke(sample, own_zid, expected_zid);
}

void verify_transport_put(const z_loaned_sample_t *sample, const z_id_t *own_zid, const z_id_t *expected_zid,
                          z_whatami_t expected_whatami) {
    assert(z_sample_kind(sample) == Z_SAMPLE_KIND_PUT);
    verify_transport_ke(sample, own_zid, expected_zid);

    z_owned_string_t json_str;
    assert(z_bytes_to_string(z_sample_payload(sample), &json_str) == Z_OK);
    json_value_t json = json_value_none();
    assert(json_deserialize(z_loan(json_str), &json) == Z_OK);
    z_string_drop(z_move(json_str));

    assert(json_value_is_object(&json));
    json_obj_t *obj = json_value_get_object(&json);
    json_value_t *zid = json_obj_get_value(obj, "zid");
    assert(zid != NULL);
    assert(json_value_is_string(zid));
    const _z_string_t *zid_str = json_value_get_string(zid);
    z_owned_string_t expected_zid_str;
    assert(z_id_to_string(expected_zid, &expected_zid_str) == Z_OK);
    assert(_z_string_equals(zid_str, z_loan(expected_zid_str)));
    z_drop(z_move(expected_zid_str));

    z_view_string_t whatami_str;
    assert(z_whatami_to_view_string(expected_whatami, &whatami_str) == Z_OK);
    json_value_t *whatami = json_obj_get_value(obj, "whatami");
    assert(whatami != NULL);
    assert(json_value_is_string(whatami));
    const _z_string_t *whatami_val_str = json_value_get_string(whatami);
    assert(_z_string_equals(whatami_val_str, z_loan(whatami_str)));

    json_value_t *is_qos = json_obj_get_value(obj, "is_qos");
    assert(is_qos != NULL);
    assert(json_value_is_boolean(is_qos));
    assert(*json_value_get_boolean(is_qos) == false);

    json_value_t *is_shm = json_obj_get_value(obj, "is_shm");
    assert(is_shm != NULL);
    assert(json_value_is_boolean(is_shm));
    assert(*json_value_get_boolean(is_shm) == false);

    json_value_destroy(&json);
}

void verify_link_ke(const z_loaned_sample_t *sample, const z_id_t *own_zid, const z_id_t *expected_zid) {
    char expected_ke_prefix[128] = {0};
    int len = snprintf(expected_ke_prefix, sizeof(expected_ke_prefix),
                       "@/"_Z_ID_PRINT_FORMAT
                       "/session/transport/unicast/"_Z_ID_PRINT_FORMAT
                       "/link/",
                       _Z_ID_PRINT_ARGS(own_zid), _Z_ID_PRINT_ARGS(expected_zid));
    z_view_string_t actual_ke;
    z_keyexpr_as_view_string(z_sample_keyexpr(sample), &actual_ke);
    assert(memcmp(expected_ke_prefix, _z_string_data(z_loan(actual_ke)), (size_t)len) == 0);
}
void verify_link_delete(const z_loaned_sample_t *sample, const z_id_t *own_zid, const z_id_t *expected_zid) {
    assert(z_sample_kind(sample) == Z_SAMPLE_KIND_DELETE);
    verify_link_ke(sample, own_zid, expected_zid);
}
void verify_link_put(const z_loaned_sample_t *sample, const z_id_t *own_zid, const z_id_t *expected_zid,
                     const char *locator) {
    assert(z_sample_kind(sample) == Z_SAMPLE_KIND_PUT);
    verify_link_ke(sample, own_zid, expected_zid);

    z_owned_string_t json_str;
    assert(z_bytes_to_string(z_sample_payload(sample), &json_str) == Z_OK);
    json_value_t json = json_value_none();
    assert(json_deserialize(z_loan(json_str), &json) == Z_OK);
    z_string_drop(z_move(json_str));

    assert(json_value_is_object(&json));
    json_obj_t *obj = json_value_get_object(&json);

    json_value_t *src = json_obj_get_value(obj, "src");
    assert(src != NULL);
    assert(json_value_is_string(src));
    assert(memcmp(_z_string_data(json_value_get_string(src)), locator, _z_string_len(json_value_get_string(src))) == 0);

    json_value_t *group = json_obj_get_value(obj, "group");
    assert(group != NULL);
    assert(json_value_is_none(group));

    json_value_t *dst = json_obj_get_value(obj, "dst");
    assert(dst != NULL);
    assert(json_value_is_string(dst));

    json_value_t *is_streamed = json_obj_get_value(obj, "is_streamed");
    assert(is_streamed != NULL);
    assert(json_value_is_boolean(is_streamed));

    json_value_t *is_reliable = json_obj_get_value(obj, "is_reliable");
    assert(is_reliable != NULL);
    assert(json_value_is_boolean(is_reliable));

    json_value_t *mtu = json_obj_get_value(obj, "mtu");
    assert(mtu != NULL);
    assert(json_value_is_number(mtu));

    json_value_destroy(&json);
}

void test_admin_space_connected_events_put(void) {
    printf("test_admin_space_connected_events_put\n");

    const char *listen_locator = "tcp/127.0.0.1:10004";

    z_owned_session_t s;
    assert(create_listen_session(&s, listen_locator, true) == Z_OK);
    z_owned_fifo_handler_sample_t transport_handler;
    z_owned_fifo_handler_sample_t link_handler;

    assert(declare_local_subscriber(z_loan(s), "@/*/session/transport/unicast/*", &transport_handler) == Z_OK);
    assert(declare_local_subscriber(z_loan(s), "@/*/session/transport/unicast/*/link/*", &link_handler) == Z_OK);

    z_owned_session_t s1;
    assert(create_connect_session(&s1, listen_locator) == Z_OK);
    z_sleep_ms(500);

    z_id_t own_zid = z_info_zid(z_loan(s));
    z_id_t s1_zid = z_info_zid(z_loan(s1));

    z_owned_sample_t sample;
    assert(z_try_recv(z_loan(link_handler), &sample) == Z_OK);
    verify_link_put(z_loan(sample), &own_zid, &s1_zid, listen_locator);
    z_sample_drop(z_move(sample));

    assert(z_try_recv(z_loan(transport_handler), &sample) == Z_OK);
    verify_transport_put(z_loan(sample), &own_zid, &s1_zid, Z_WHATAMI_PEER);
    z_sample_drop(z_move(sample));

    z_drop(z_move(s1));

    z_sleep_ms(500);
    assert(z_try_recv(z_loan(link_handler), &sample) == Z_OK);
    verify_link_delete(z_loan(sample), &own_zid, &s1_zid);
    z_sample_drop(z_move(sample));

    assert(z_try_recv(z_loan(transport_handler), &sample) == Z_OK);
    verify_transport_delete(z_loan(sample), &own_zid, &s1_zid, Z_WHATAMI_PEER);
    z_sample_drop(z_move(sample));

    z_fifo_handler_sample_drop(z_move(link_handler));
    z_fifo_handler_sample_drop(z_move(transport_handler));

    z_drop(z_move(s));
}

void test_admin_space_connected_events_query(void) {
    printf("test_admin_space_connected_events_query\n");

    const char *listen_locator = "tcp/127.0.0.1:10005";

    z_owned_session_t s;
    assert(create_listen_session(&s, listen_locator, true) == Z_OK);
    z_owned_fifo_handler_reply_t link_handler;
    z_owned_fifo_handler_reply_t transport_handler;

    z_owned_reply_t reply;

    assert(make_session_get(z_loan(s), "@/*/session/transport/unicast/*/link/*", &link_handler) == Z_OK);
    assert(z_try_recv(z_loan(link_handler), &reply) == Z_CHANNEL_DISCONNECTED);
    assert(make_session_get(z_loan(s), "@/*/session/transport/unicast/*", &transport_handler) == Z_OK);
    assert(z_try_recv(z_loan(transport_handler), &reply) == Z_CHANNEL_DISCONNECTED);

    z_fifo_handler_reply_drop(z_move(link_handler));
    z_fifo_handler_reply_drop(z_move(transport_handler));

    z_owned_session_t sessions[2];
    assert(create_connect_session(&sessions[0], listen_locator) == Z_OK);
    assert(create_connect_session(&sessions[1], listen_locator) == Z_OK);
    z_sleep_ms(500);

    z_id_t own_zid = z_info_zid(z_loan(s));

    for (size_t i = 0; i < 2; i++) {
        z_id_t session_zid = z_info_zid(z_loan(sessions[i]));
        char buf[128] = {0};
        snprintf(buf, sizeof(buf),
                 "@/*/session/transport/unicast/"_Z_ID_PRINT_FORMAT
                 "/link/*",
                 _Z_ID_PRINT_ARGS(&session_zid));
        assert(make_session_get(z_loan(s), buf, &link_handler) == Z_OK);
        assert(z_try_recv(z_loan(link_handler), &reply) == Z_OK);
        assert(z_reply_is_ok(z_loan(reply)));
        verify_link_put(z_reply_ok(z_loan(reply)), &own_zid, &session_zid, listen_locator);
        z_reply_drop(z_move(reply));
        assert(z_try_recv(z_loan(link_handler), &reply) == Z_CHANNEL_DISCONNECTED);

        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "@/*/session/transport/unicast/"_Z_ID_PRINT_FORMAT, _Z_ID_PRINT_ARGS(&session_zid));
        assert(make_session_get(z_loan(s), buf, &transport_handler) == Z_OK);
        assert(z_try_recv(z_loan(transport_handler), &reply) == Z_OK);
        assert(z_reply_is_ok(z_loan(reply)));
        verify_transport_put(z_reply_ok(z_loan(reply)), &own_zid, &session_zid, Z_WHATAMI_PEER);
        z_reply_drop(z_move(reply));
        assert(z_try_recv(z_loan(transport_handler), &reply) == Z_CHANNEL_DISCONNECTED);

        z_fifo_handler_reply_drop(z_move(link_handler));
        z_fifo_handler_reply_drop(z_move(transport_handler));
    }

    z_drop(z_move(sessions[0]));
    z_drop(z_move(sessions[1]));
    z_drop(z_move(s));
}
#endif

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    test_start_stop_admin_space();
    test_auto_start_admin_space();
    test_admin_space_no_connections();
    test_admin_space_with_connections();
#if Z_FEATURE_CONNECTIVITY == 1 && Z_FEATURE_LOCAL_SUBSCRIBER == 1
    test_admin_space_connected_events_put();
    test_admin_space_connected_events_query();
#endif
    return 0;
}

#else

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf(
        "Missing config token to run this test. This test requires: Z_FEATURE_ADMIN_SPACE == 1 and "
        "Z_FEATURE_UNICAST_PEER == 1\n");
    return 0;
}

#endif  // Z_FEATURE_ADMIN_SPACE == 1 && Z_FEATURE_UNICAST_PEER == 1
