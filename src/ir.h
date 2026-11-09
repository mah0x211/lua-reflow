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

#ifndef REFLOW_IR_H
#define REFLOW_IR_H

#include <lauxlib.h>
#include <lua.h>
#include <stddef.h>

#define REFLOW_IR_MT "reflow.ir"

typedef struct ir_t ir_t;
typedef struct ir_node_t ir_node_t;
typedef struct ir_attr_t ir_attr_t;
typedef struct ir_branch_t ir_branch_t;
typedef struct ir_directives_t ir_directives_t;
typedef struct ir_bind_t ir_bind_t;
typedef struct ir_with_t ir_with_t;
typedef struct ir_for_t ir_for_t;
typedef struct ir_each_t ir_each_t;
typedef struct ir_dom_index_t ir_dom_index_t;
struct reflow_value_t;
struct reflow_expression_t;

typedef enum ir_node_type_t {
    IR_ROOT = 1,
    IR_ELEMENT,
    IR_CHAIN,
    IR_TEXT,
    IR_COMMENT
} ir_node_type_t;

typedef struct ir_string_t {
    const char *data;
    size_t len;
} ir_string_t;

typedef struct ir_node_list_t {
    ir_node_t *head;
    ir_node_t *tail;
    size_t count;
} ir_node_list_t;

typedef struct ir_attr_list_t {
    ir_attr_t *head;
    ir_attr_t *tail;
    size_t count;
} ir_attr_list_t;

typedef struct ir_bind_list_t {
    ir_bind_t *head;
    ir_bind_t *tail;
    size_t count;
} ir_bind_list_t;

typedef struct ir_with_list_t {
    ir_with_t *head;
    ir_with_t *tail;
    size_t count;
} ir_with_list_t;

typedef struct ir_branch_list_t {
    ir_branch_t *head;
    ir_branch_t *tail;
    size_t count;
} ir_branch_list_t;

struct ir_node_t {
    ir_node_type_t type;
    ir_node_t *next;
};

typedef struct ir_root_t {
    ir_node_t node;
    ir_node_list_t children;
} ir_root_t;

typedef enum ir_branch_kind_t {
    IR_BRANCH_NONE = 0,
    IR_BRANCH_CHAIN,
    IR_BRANCH_MATCH
} ir_branch_kind_t;

/* Structural annotations of an element, filled in once the IR is complete.
 * Chains are transparent: a chain branch takes the parent and depth of the
 * chain itself. An x-case / x-nocase branch takes its x-match element as
 * parent. */
typedef struct ir_structure_t {
    const struct ir_element_t *parent; /* NULL at the top level */
    size_t depth;                      /* 0 at the top level */
    size_t order;                      /* 0-based document order */
    ir_branch_kind_t branch_kind;
    const struct ir_chain_t *chain; /* set for IR_BRANCH_CHAIN */
    size_t branch_index;            /* 0-based, unless IR_BRANCH_NONE */
} ir_structure_t;

typedef struct ir_element_t {
    ir_node_t node;
    ir_string_t tag_name;
    ir_attr_list_t attrs;
    ir_directives_t *directives;
    ir_node_list_t children;
    size_t source_start;
    size_t source_end;
    ir_structure_t structure;
} ir_element_t;

typedef struct ir_chain_t {
    ir_node_t node;
    ir_branch_list_t branches;
    size_t source_start;
    size_t source_end;
} ir_chain_t;

typedef struct ir_text_t {
    ir_node_t node;
    ir_string_t text;
} ir_text_t;

typedef struct ir_comment_t {
    ir_node_t node;
    ir_string_t text;
} ir_comment_t;

struct ir_directives_t {
    const struct reflow_value_t *data_scopes;
    const struct reflow_expression_t *text;
    const struct reflow_expression_t *html;
    const struct reflow_expression_t *include;
    ir_bind_list_t bind;
    ir_with_list_t with;
    const struct reflow_expression_t *if_condition;
    const struct reflow_expression_t *elseif_condition;
    int has_else;
    const struct reflow_expression_t *match_condition;
    const struct reflow_expression_t *case_condition;
    int has_nocase;
    /* The x-case / x-nocase children collected when an x-match element
     * closes; they replace its children. condition is NULL for x-nocase. */
    ir_branch_list_t match_branches;
    const ir_for_t *range;
    const ir_each_t *each;
    int has_break;
    const struct reflow_expression_t *break_condition;
    /* Set on an element whose only directives are x-break / x-break-if and
     * which has no regular attribute; such an element renders nothing. */
    int invisible_marker;
};

/* An x-each loop. index.data is NULL when no index name is given. */
struct ir_each_t {
    ir_string_t item;
    ir_string_t index;
    const struct reflow_expression_t *collection;
};

/* An x-for range, fixed at compile time. Bounds are inclusive and step is
 * never zero; -0 is stored as +0. */
struct ir_for_t {
    ir_string_t name;
    double start;
    double stop;
    double step;
};

/* One x-with binding, kept in source order. */
struct ir_with_t {
    ir_with_t *next;
    ir_string_t name;
    const struct reflow_expression_t *expression;
};

/* One x-bind:<name> directive, kept in source order. */
struct ir_bind_t {
    ir_bind_t *next;
    ir_string_t name;
    const struct reflow_expression_t *expression;
};

struct ir_attr_t {
    ir_attr_t *next;
    ir_string_t name;
    ir_string_t value;
};

/* One branch of an x-if chain. condition is NULL for the x-else branch. */
struct ir_branch_t {
    ir_branch_t *next;
    const struct reflow_expression_t *condition;
    ir_node_t *node;
};

/* Elements in document order. */
typedef struct ir_element_list_t {
    const ir_element_t **items;
    size_t count;
    size_t capacity;
} ir_element_list_t;

typedef struct ir_index_entry_t ir_index_entry_t;

struct ir_index_entry_t {
    ir_index_entry_t *next;      /* next key in first-seen order */
    ir_index_entry_t *slot_next; /* next key in the same hash slot */
    size_t hash;
    ir_string_t key;
    ir_element_list_t elements;
};

/* A hash table from byte-string keys to element lists. An element is listed
 * once per occurrence, so a repeated attribute or class lists it again, as
 * in the JavaScript version. */
typedef struct ir_index_axis_t {
    ir_index_entry_t **slots;
    size_t slot_count;
    ir_index_entry_t *head;
    ir_index_entry_t *tail;
    size_t count;
} ir_index_axis_t;

/* The static DOM index of a compiled template. Only regular attributes are
 * indexed; attributes produced by x-bind at render time are not. */
struct ir_dom_index_t {
    ir_index_axis_t by_id;
    ir_index_axis_t by_class;
    ir_index_axis_t by_tag;
    ir_index_axis_t by_attr_name;
    ir_element_list_t elements;
    ir_element_list_t includes;
};

struct ir_t {
    ir_root_t *root;
    ir_string_t name;
    ir_string_t html;
    const ir_dom_index_t *index;
};

static inline ir_t *ir_check(lua_State *L, int idx)
{
    return (ir_t *)luaL_checkudata(L, idx, REFLOW_IR_MT);
}

#endif /* REFLOW_IR_H */
