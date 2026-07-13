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
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef REFLOW_EXPR_NODE_H
#define REFLOW_EXPR_NODE_H

#include <stddef.h>
#include <stdbool.h>
#include "../value.h"

/* AST node types */
enum ex_type {
    EX_LITERAL,   /* true/false/null/number/string */
    EX_DOLLAR,    /* $ */
    EX_AT,        /* @name */
    EX_DOT,       /* .name */
    EX_MEMBER,    /* object.property or object?.property */
    EX_UNARY,     /* !arg */
    EX_BINARY,    /* left op right */
    EX_TERNARY,   /* test ? cons : alt */
    EX_CALL,      /* callee(args) */
    EX_OBJECT,    /* { key: val, ... } */
    EX_ARRAY,     /* [ a, b, ... ] */
};

/* Binary operators (ported from evaluate.js switch) */
enum bin_op {
    OP_EQ,        /* == */
    OP_NE,        /* != */
    OP_LT,        /* < */
    OP_GT,        /* > */
    OP_LE,        /* <= */
    OP_GE,        /* >= */
    OP_AND,       /* && */
    OP_OR,        /* || */
    OP_COALESCE,  /* ?? */
};

/* Object literal entry */
typedef struct obj_entry {
    bool              computed;  /* true if [expr] key */
    char             *key;       /* literal key string (malloc-owned, NUL-terminated) */
    size_t            klen;
    struct expr_node *key_expr;  /* computed key expression (NULL when !computed) */
    struct expr_node *val;       /* value expression */
} obj_entry;

/* AST node (tagged union) */
typedef struct expr_node {
    enum ex_type type;
    size_t       offset;         /* source byte offset for error reporting */

    union {
        /* EX_LITERAL: holds a reflow_value (number/string/bool/null) */
        reflow_value literal;

        /* EX_AT / EX_DOT: scope reference name */
        struct { char *name; } at;
        struct { char *name; } dot;

        /* EX_MEMBER: object.property or object?.property */
        struct {
            struct expr_node *object;
            char             *pname;     /* property name (malloc-owned) */
            bool              optional;  /* ?. vs . */
        } member;

        /* EX_UNARY: !arg (only operator) */
        struct { struct expr_node *arg; } unary;

        /* EX_BINARY: left op right */
        struct {
            enum bin_op       op;
            struct expr_node *left;
            struct expr_node *right;
        } binary;

        /* EX_TERNARY: test ? cons : alt */
        struct {
            struct expr_node *test;
            struct expr_node *cons;
            struct expr_node *alt;
        } ternary;

        /* EX_CALL: callee(args) — callee is a helper name string */
        struct {
            char             *callee;
            struct expr_node **args;
            size_t            n_args;
        } call;

        /* EX_OBJECT: { key: val, ... } */
        struct {
            obj_entry *entries;
            size_t     n;
        } object;

        /* EX_ARRAY: [ a, b, ... ] */
        struct {
            struct expr_node **items;
            size_t             n;
        } array;
    };
} expr_node;

/*
 * AST nodes are allocated from compile_arena (GC-managed).
 * Individual free is neither needed nor possible — the entire arena
 * is collected by Lua's GC when the arena userdata becomes unreachable.
 */

#endif /* REFLOW_EXPR_NODE_H */
