/*
 * Copyright (C) 2026 Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef REFLOW_EXPR_H
#define REFLOW_EXPR_H

#include <stddef.h>

#include "value.h"

struct pool_t;
typedef struct reflow_expr_t reflow_expr_t;
typedef struct reflow_expr_object_entry_t reflow_expr_object_entry_t;

typedef enum reflow_expr_type_t {
    REFLOW_EXPR_LITERAL = 1,
    REFLOW_EXPR_DOLLAR,
    REFLOW_EXPR_AT,
    REFLOW_EXPR_DOT,
    REFLOW_EXPR_MEMBER,
    REFLOW_EXPR_UNARY,
    REFLOW_EXPR_BINARY,
    REFLOW_EXPR_TERNARY,
    REFLOW_EXPR_CALL,
    REFLOW_EXPR_OBJECT,
    REFLOW_EXPR_ARRAY
} reflow_expr_type_t;

typedef enum reflow_expr_operator_t {
    REFLOW_EXPR_NOT = 1,
    REFLOW_EXPR_EQ,
    REFLOW_EXPR_NE,
    REFLOW_EXPR_LE,
    REFLOW_EXPR_GE,
    REFLOW_EXPR_LT,
    REFLOW_EXPR_GT,
    REFLOW_EXPR_AND,
    REFLOW_EXPR_OR,
    REFLOW_EXPR_COALESCE
} reflow_expr_operator_t;

typedef struct reflow_expr_string_t {
    const char *data;
    size_t len;
} reflow_expr_string_t;

typedef struct reflow_expr_list_t {
    reflow_expr_t *head;
    reflow_expr_t *tail;
    size_t count;
} reflow_expr_list_t;

typedef struct reflow_expr_object_t {
    reflow_expr_object_entry_t *head;
    reflow_expr_object_entry_t *tail;
    size_t count;
} reflow_expr_object_t;

struct reflow_expr_t {
    reflow_expr_type_t type;
    reflow_expr_t *next;
    size_t offset;
    size_t length;
    union {
        const reflow_value_t *literal;
        struct {
            reflow_expr_string_t name;
        } scope;
        struct {
            reflow_expr_t *object;
            reflow_expr_string_t property;
            int optional;
        } member;
        struct {
            reflow_expr_operator_t op;
            reflow_expr_t *argument;
        } unary;
        struct {
            reflow_expr_operator_t op;
            reflow_expr_t *left;
            reflow_expr_t *right;
        } binary;
        struct {
            reflow_expr_t *test;
            reflow_expr_t *consequent;
            reflow_expr_t *alternate;
        } ternary;
        struct {
            reflow_expr_string_t callee;
            reflow_expr_list_t arguments;
        } call;
        reflow_expr_object_t object;
        reflow_expr_list_t array;
    } as;
};

struct reflow_expr_object_entry_t {
    reflow_expr_object_entry_t *next;
    reflow_expr_t *key;
    reflow_expr_t *value;
    int computed;
};

typedef struct reflow_expression_t {
    reflow_expr_string_t source;
    reflow_expr_t *root;
} reflow_expression_t;

typedef enum reflow_expr_error_kind_t {
    REFLOW_EXPR_ERROR_PARSE = 1,
    REFLOW_EXPR_ERROR_OOM
} reflow_expr_error_kind_t;

/* A parse error describes the failure with a static message and the byte
 * position in the source where parsing stopped. When the message contains
 * "%D", each occurrence stands for the source bytes [detail, detail + len);
 * the consumer substitutes them to build the full diagnostic. */
typedef struct reflow_expr_error_t {
    reflow_expr_error_kind_t kind;
    const char *message;
    size_t position;
    size_t detail;
    size_t detail_len;
} reflow_expr_error_t;

/* The result pool owns the returned immutable tree. The caller may release or
 * reuse the scratch pool immediately after this function returns. */
int reflow_expr_parse(struct pool_t *result_pool, struct pool_t *scratch_pool,
                      const char *source, size_t source_len,
                      reflow_expression_t **result, reflow_expr_error_t *error);

#endif /* REFLOW_EXPR_H */
