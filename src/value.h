/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#ifndef REFLOW_VALUE_H
#define REFLOW_VALUE_H

#include <stddef.h>
#include <stdbool.h>

/* JS value tag (see docs/design/02-value-model.md, 04-runtime.md §2.1). */
typedef enum {
    RV_NULL,       /* JS null */
    RV_UNDEFINED,  /* JS undefined (unresolved variable / missing property) */
    RV_NUMBER,
    RV_STRING,
    RV_BOOL,
    RV_ARRAY,
    RV_OBJECT,
} rv_tag;

struct rv_prop; /* forward declaration (object entry holds a value by value) */

typedef struct reflow_value {
    rv_tag tag;
    union {
        double number;
        struct {
            const char *data;
            size_t len;
        } string; /* referenced or arena-owned, not NUL-terminated */
        bool boolean;
        struct {
            struct reflow_value *items;
            size_t len;
            size_t cap;
        } array;
        struct {
            struct rv_prop *props;
            size_t len;
            size_t cap;
        } object; /* insertion-ordered */
    };
} reflow_value;

typedef struct rv_prop {
    const char *key;
    size_t key_len;
    reflow_value value;
} rv_prop;

/* --- JS semantics (see docs/design/02-value-model.md §2) --- */

/* null or undefined (?? / optional chaining). */
int rv_is_nullish(const reflow_value *v);
/* false/0/''/NaN/null/undefined are falsy; objects are always truthy. */
int rv_is_falsy(const reflow_value *v);
int rv_is_truthy(const reflow_value *v);
/* === / !== ; arrays/objects compare by pointer identity. */
int rv_strict_eq(const reflow_value *a, const reflow_value *b);
int rv_strict_neq(const reflow_value *a, const reflow_value *b);
/* < > <= >= : returns -1/0/1, or 2 for not-comparable (NaN / mixed types). */
int rv_compare(const reflow_value *a, const reflow_value *b);
/* Array.isArray equivalent. */
int rv_is_array(const reflow_value *v);

/*
 * String(v)-compatible serialization into `out` (caller ensures >= 32 bytes).
 * Returns the length written. null/undefined/array/object write nothing and
 * return 0 (caller decides omit/empty per x-text/x-bind).
 */
size_t rv_to_js_string(const reflow_value *v, char *out);

/*
 * number_to_string: JS String(n)-compatible formatting.
 * Verified against JS String(n) on 12043 cases (bench/value_proto.c).
 * `out` must be >= 32 bytes. Returns the length written.
 */
size_t number_to_string(double n, char *out);

/* --- scalar constructors --- */
reflow_value rv_number(double n);
reflow_value rv_string(const char *data, size_t len);
reflow_value rv_bool(int b);
reflow_value rv_null(void);
reflow_value rv_undef(void);

#endif /* REFLOW_VALUE_H */
