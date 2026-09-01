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

typedef struct ir_element_t {
    ir_node_t node;
    ir_string_t tag_name;
    ir_attr_list_t attrs;
    ir_node_list_t children;
    size_t source_start;
    size_t source_end;
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

struct ir_attr_t {
    ir_attr_t *next;
    ir_string_t name;
    ir_string_t value;
};

struct ir_branch_t {
    ir_branch_t *next;
    const void *condition;
    ir_node_t *node;
};

struct ir_t {
    ir_root_t *root;
    ir_string_t name;
    ir_string_t html;
};

static inline ir_t *ir_check(lua_State *L, int idx)
{
    return (ir_t *)luaL_checkudata(L, idx, REFLOW_IR_MT);
}

#endif /* REFLOW_IR_H */
