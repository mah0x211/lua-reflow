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

#ifndef REFLOW_IR_H
#define REFLOW_IR_H

#include <stdbool.h>
#include <stddef.h>
#include "compile_arena.h"
#include "expr/parse.h"
#include "value.h"

/* ── IR node types ────────────────────────────────────────── */

typedef enum {
    IR_ROOT,        /* top-level container */
    IR_ELEMENT,     /* HTML element */
    IR_TEXT,        /* text node */
    IR_COMMENT,     /* comment node */
    IR_CHAIN,       /* synthetic: consolidated if/elseif/else */
} ir_type;

/* ── Attribute (name-value pair, non-directive) ───────────── */

typedef struct {
    const char *name;
    const char *value;   /* may be NULL for valueless attribute */
} ir_attr;

/* ── x-bind entry ─────────────────────────────────────────── */

typedef struct {
    const char *attr_name;
    expr_node  *expr;
} ir_bind;

/* ── x-with binding ───────────────────────────────────────── */

typedef struct {
    const char *name;
    expr_node  *expr;
} ir_with_binding;

/* ── x-for loop spec ──────────────────────────────────────── */

typedef struct {
    char  *var_name;
    double start;
    double stop;
    double step;
} ir_for_spec;

/* ── x-each iteration spec ────────────────────────────────── */

typedef struct {
    char       *item_name;
    char       *index_name;   /* NULL if not specified */
    expr_node  *collection;
} ir_each_spec;

/* ── Chain / match branch ─────────────────────────────────── */

typedef struct {
    expr_node      *cond;   /* NULL for else / nocase */
    struct ir_node *node;
} ir_branch;

/* ── Match directive (post-processed) ─────────────────────── */

typedef struct {
    expr_node   *expr;
    ir_branch   *branches;
    size_t       n_branches;
} ir_match;

/* ── Directives (all optional, NULL/zero if absent) ───────── */

typedef struct {
    reflow_value     *data;             /* x-data: parsed JSON5 scopes */
    /* x-with */
    ir_with_binding  *with_bindings;
    size_t            n_with;
    /* content directives */
    expr_node        *text_expr;        /* x-text */
    expr_node        *html_expr;        /* x-html */
    expr_node        *include_expr;     /* x-include */
    /* iteration */
    ir_for_spec      *for_spec;         /* x-for (NULL if absent) */
    ir_each_spec     *each_spec;        /* x-each (NULL if absent) */
    /* conditional (raw markers, consumed by post-processing) */
    expr_node        *if_expr;
    expr_node        *elseif_expr;
    expr_node        *match_expr;
    expr_node        *case_expr;
    bool              else_mark;
    bool              nocase_mark;
    /* break */
    bool              break_mark;
    expr_node        *break_if_expr;
    /* x-bind */
    ir_bind          *binds;
    size_t            n_binds;
    /* match (post-processed) */
    ir_match         *match;
} ir_directives;

/* ── IR node (tagged union) ───────────────────────────────── */

typedef struct ir_node {
    ir_type type;
    union {
        /* IR_ROOT */
        struct {
            struct ir_node **children;
            size_t           n_children;
        } root;

        /* IR_ELEMENT */
        struct {
            char           *tag_name;
            ir_attr        *attrs;
            size_t          n_attrs;
            ir_directives   directives;
            struct ir_node **children;
            size_t          n_children;
            size_t          source_start;
            size_t          source_end;
            bool            invisible_marker;
        } element;

        /* IR_TEXT */
        struct {
            char  *text;
            size_t text_len;
        } text;

        /* IR_COMMENT */
        struct {
            char  *text;
            size_t text_len;
        } comment;

        /* IR_CHAIN */
        struct {
            ir_branch *branches;
            size_t     n_branches;
            size_t     source_start;
            size_t     source_end;
        } chain;
    };
} ir_node;

/* ── Factory functions (all allocate from compile_arena) ──── */

ir_node *ir_make_root(compile_arena *arena, lua_State *L);

ir_node *ir_make_element(compile_arena *arena, lua_State *L,
                         char *tag_name,
                         size_t source_start, size_t source_end);

ir_node *ir_make_text(compile_arena *arena, lua_State *L,
                      char *text, size_t text_len);

ir_node *ir_make_comment(compile_arena *arena, lua_State *L,
                         char *text, size_t text_len);

ir_node *ir_make_chain(compile_arena *arena, lua_State *L,
                       ir_branch *branches, size_t n_branches,
                       size_t source_start, size_t source_end);

/* ── Child / attribute management ─────────────────────────── */

/* Append child to parent's children array (grows via arena). */
void ir_add_child(compile_arena *arena, lua_State *L,
                  ir_node *parent, ir_node *child);

/* Append attribute to element's attrs array. */
void ir_add_attr(compile_arena *arena, lua_State *L,
                 ir_node *element, const char *name, const char *value);

/* ── Helpers ──────────────────────────────────────────────── */

/* True if node is whitespace-only text or a comment. */
bool ir_is_ignorable(const ir_node *node);

#endif /* REFLOW_IR_H */
