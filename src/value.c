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

// project
#include "value.h"
#include "checked.h"
// system
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- number_to_string: JS String(n)-compatible (ECMAScript Number::toString) --- */
size_t number_to_string(double n, char *out)
{
    if (isnan(n)) {
        strcpy(out, "NaN");
        return 3;
    }
    if (isinf(n)) {
        if (n > 0) {
            strcpy(out, "Infinity");
            return 8;
        }
        strcpy(out, "-Infinity");
        return 9;
    }
    if (n == 0.0) {
        strcpy(out, "0");
        return 1;
    }

    int neg = (n < 0);
    double a = neg ? -n : n;

    /* Shortest round-trip: first precision whose text parses back to `a`. */
    char ebuf[64];
    int prec;
    for (prec = 0; prec <= 17; prec++) {
        snprintf(ebuf, sizeof(ebuf), "%.*e", prec, a);
        if (strtod(ebuf, NULL) == a) {
            break;
        }
    }

    /* Parse "d[.ddd]e(+|-)dd" into a digit string and decimal exponent. */
    char digits[40];
    int ndig = 0;
    const char *p = ebuf;
    digits[ndig++] = *p++;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            digits[ndig++] = *p++;
        }
    }
    p++; /* skip 'e' */
    int exp10 = atoi(p);
    while (ndig > 1 && digits[ndig - 1] == '0') {
        ndig--;
    }
    digits[ndig] = '\0';

    /* ECMAScript formatting cases (n = exp10 + 1, k = ndig). */
    int k = ndig;
    int n_ecma = exp10 + 1;
    char body[64];
    body[0] = '\0';

    if (k <= n_ecma && n_ecma <= 21) {
        /* integer with trailing zeros */
        strcpy(body, digits);
        for (int i = 0; i < n_ecma - k; i++) {
            strcat(body, "0");
        }
    } else if (0 < n_ecma && n_ecma <= 21) {
        /* fixed point */
        memcpy(body, digits, n_ecma);
        body[n_ecma] = '.';
        memcpy(body + n_ecma + 1, digits + n_ecma, k - n_ecma);
        body[n_ecma + 1 + (k - n_ecma)] = '\0';
    } else if (-6 < n_ecma && n_ecma <= 0) {
        /* 0.00..0digits */
        strcpy(body, "0.");
        for (int i = 0; i < -n_ecma; i++) {
            strcat(body, "0");
        }
        strcat(body, digits);
    } else {
        /* exponential d[.ddd]e(+|-)dd */
        char mant[40];
        if (k == 1) {
            strcpy(mant, digits);
        } else {
            mant[0] = digits[0];
            mant[1] = '.';
            strcpy(mant + 2, digits + 1);
        }
        snprintf(body, sizeof(body), "%se%c%d", mant,
                 exp10 < 0 ? '-' : '+', abs(exp10));
    }

    if (neg) {
        out[0] = '-';
        strcpy(out + 1, body);
        return strlen(out);
    }
    strcpy(out, body);
    return strlen(out);
}

/* --- rv_to_js_string: String(v) compatible --- */
size_t rv_to_js_string(const reflow_value *v, char *out)
{
    switch (v->tag) {
    case RV_BOOL:
        strcpy(out, v->boolean ? "true" : "false");
        return strlen(out);
    case RV_NUMBER:
        return number_to_string(v->number, out);
    case RV_STRING:
        memcpy(out, v->string.data, v->string.len);
        out[v->string.len] = '\0';
        return v->string.len;
    default:
        /* null/undefined/array/object: caller decides (omit or empty). */
        out[0] = '\0';
        return 0;
    }
}

/* --- JS semantics (docs/design/02-value-model.md §2) --- */

int rv_is_nullish(const reflow_value *v)
{
    return v->tag == RV_NULL || v->tag == RV_UNDEFINED;
}

int rv_is_falsy(const reflow_value *v)
{
    switch (v->tag) {
    case RV_UNDEFINED:
    case RV_NULL:
        return 1;
    case RV_BOOL:
        return !v->boolean;
    case RV_NUMBER:
        return v->number == 0.0 || isnan(v->number);
    case RV_STRING:
        return v->string.len == 0;
    case RV_ARRAY:
    case RV_OBJECT:
        return 0; /* JS objects are always truthy */
    }
    return 1;
}

int rv_is_truthy(const reflow_value *v)
{
    return !rv_is_falsy(v);
}

int rv_strict_eq(const reflow_value *a, const reflow_value *b)
{
    if (a->tag == RV_NULL && b->tag == RV_NULL) {
        return 1;
    }
    if (a->tag == RV_UNDEFINED && b->tag == RV_UNDEFINED) {
        return 1;
    }
    if (rv_is_nullish(a) || rv_is_nullish(b)) {
        return 0; /* one side only */
    }
    if (a->tag != b->tag) {
        return 0; /* 1 === '1' is false */
    }
    switch (a->tag) {
    case RV_BOOL:
        return a->boolean == b->boolean;
    case RV_NUMBER:
        if (isnan(a->number) || isnan(b->number)) {
            return 0; /* NaN !== NaN */
        }
        return a->number == b->number;
    case RV_STRING:
        return a->string.len == b->string.len &&
               memcmp(a->string.data, b->string.data, a->string.len) == 0;
    case RV_ARRAY:
        return a->array.items == b->array.items;
    case RV_OBJECT:
        return a->object.props == b->object.props;
    default:
        return 0;
    }
}

int rv_strict_neq(const reflow_value *a, const reflow_value *b)
{
    return !rv_strict_eq(a, b);
}

static bool ascii_trim_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

/* ECMAScript ToNumber subset used by relational string/number comparisons.
 * Expression string literals are byte strings; validate the entire trimmed
 * token before delegating decimal conversion to strtod. */
static bool string_to_number(const reflow_value *value, arena_t *arena,
                             double *out)
{
    size_t begin = 0;
    size_t end = value->string.len;
    const char *src = value->string.data;
    while (begin < end && ascii_trim_space((unsigned char)src[begin])) begin++;
    while (end > begin && ascii_trim_space((unsigned char)src[end - 1])) end--;
    if (begin == end) {
        *out = 0.0;
        return true;
    }

    size_t len = end - begin;
    const char *text = src + begin;
    if ((len == 8 && memcmp(text, "Infinity", 8) == 0) ||
        (len == 9 && memcmp(text, "+Infinity", 9) == 0)) {
        *out = INFINITY;
        return true;
    }
    if (len == 9 && memcmp(text, "-Infinity", 9) == 0) {
        *out = -INFINITY;
        return true;
    }

    if (len > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X' ||
         text[1] == 'o' || text[1] == 'O' ||
         text[1] == 'b' || text[1] == 'B')) {
        unsigned int base = (text[1] == 'x' || text[1] == 'X') ? 16U
            : (text[1] == 'o' || text[1] == 'O') ? 8U : 2U;
        double number = 0.0;
        for (size_t i = 2; i < len; i++) {
            unsigned char c = (unsigned char)text[i];
            unsigned int digit;
            if (c >= '0' && c <= '9') digit = (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = 10U + (unsigned int)(c - 'a');
            else if (c >= 'A' && c <= 'F') digit = 10U + (unsigned int)(c - 'A');
            else return false;
            if (digit >= base) return false;
            number = number * (double)base + (double)digit;
        }
        *out = number;
        return true;
    }

    size_t pos = 0;
    if (text[pos] == '+' || text[pos] == '-') pos++;
    size_t integer_digits = 0;
    while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
        pos++;
        integer_digits++;
    }
    size_t fraction_digits = 0;
    if (pos < len && text[pos] == '.') {
        pos++;
        while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
            pos++;
            fraction_digits++;
        }
    }
    if (integer_digits == 0 && fraction_digits == 0) return false;
    if (pos < len && (text[pos] == 'e' || text[pos] == 'E')) {
        pos++;
        if (pos < len && (text[pos] == '+' || text[pos] == '-')) pos++;
        size_t exponent_digits = 0;
        while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
            pos++;
            exponent_digits++;
        }
        if (exponent_digits == 0) return false;
    }
    if (pos != len) return false;

    size_t bytes = 0;
    if (!reflow_size_add(len, 1, &bytes)) return false;
    char *copy = (char *)arena_alloc(arena, bytes);
    if (copy == NULL) return false;
    memcpy(copy, text, len);
    copy[len] = '\0';
    char *parsed_end = NULL;
    *out = strtod(copy, &parsed_end);
    return parsed_end == copy + len;
}

int rv_compare(const reflow_value *a, const reflow_value *b, arena_t *arena)
{
    if (a->tag == RV_NUMBER && b->tag == RV_NUMBER) {
        if (isnan(a->number) || isnan(b->number)) {
            return 2;
        }
        if (a->number < b->number) {
            return -1;
        }
        if (a->number > b->number) {
            return 1;
        }
        return 0;
    }
    if (a->tag == RV_STRING && b->tag == RV_STRING) {
        size_t n = a->string.len < b->string.len ? a->string.len
                                                  : b->string.len;
        int c = memcmp(a->string.data, b->string.data, n);
        if (c != 0) {
            return c < 0 ? -1 : 1;
        }
        if (a->string.len < b->string.len) {
            return -1;
        }
        if (a->string.len > b->string.len) {
            return 1;
        }
        return 0;
    }
    if ((a->tag == RV_NUMBER && b->tag == RV_STRING) ||
        (a->tag == RV_STRING && b->tag == RV_NUMBER)) {
        double left = a->number;
        double right = b->number;
        if (a->tag == RV_STRING && !string_to_number(a, arena, &left)) return 2;
        if (b->tag == RV_STRING && !string_to_number(b, arena, &right)) return 2;
        if (isnan(left) || isnan(right)) return 2;
        if (left < right) return -1;
        if (left > right) return 1;
        return 0;
    }
    return 2;
}

int rv_is_array(const reflow_value *v)
{
    return v->tag == RV_ARRAY;
}

reflow_value *rv_object_get(reflow_value *obj, const char *key, size_t key_len)
{
    if (obj->tag != RV_OBJECT) return NULL;
    for (size_t i = 0; i < obj->object.len; i++) {
        rv_prop *p = &obj->object.props[i];
        if (p->key_len == key_len &&
            memcmp(p->key, key, key_len) == 0)
            return &p->value;
    }
    return NULL;
}

/* --- scalar constructors --- */

reflow_value rv_number(double n)
{
    reflow_value v;
    v.tag = RV_NUMBER;
    v.number = n;
    return v;
}

reflow_value rv_string(const char *data, size_t len)
{
    reflow_value v;
    v.tag = RV_STRING;
    v.string.data = data;
    v.string.len = len;
    return v;
}

reflow_value rv_bool(int b)
{
    reflow_value v;
    v.tag = RV_BOOL;
    v.boolean = b ? 1 : 0;
    return v;
}

reflow_value rv_null(void)
{
    reflow_value v;
    v.tag = RV_NULL;
    return v;
}

reflow_value rv_undef(void)
{
    reflow_value v;
    v.tag = RV_UNDEFINED;
    return v;
}
