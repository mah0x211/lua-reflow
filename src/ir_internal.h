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

#ifndef REFLOW_IR_INTERNAL_H
#define REFLOW_IR_INTERNAL_H

#include <lua.h>
#include <stddef.h>

#include "ir.h"
#include "pool.h"

typedef struct ir_node_private_t {
    ir_t *owner;
    ir_node_t *parent;
    unsigned int linked;
} ir_node_private_t;

typedef struct ir_owner_internal_t {
    ir_t public;
    pool_t *pool;
} ir_owner_internal_t;

typedef struct ir_root_internal_t {
    ir_root_t public;
    ir_node_private_t private;
} ir_root_internal_t;

typedef struct ir_element_internal_t {
    ir_element_t public;
    ir_node_private_t private;
} ir_element_internal_t;

typedef struct ir_text_internal_t {
    ir_text_t public;
    ir_node_private_t private;
} ir_text_internal_t;

typedef struct ir_comment_internal_t {
    ir_comment_t public;
    ir_node_private_t private;
} ir_comment_internal_t;

void ir_init_metatable(lua_State *L);
ir_t *ir_new(lua_State *L);
int ir_set_source(ir_t *ir, const char *name, size_t name_len, const char *html,
                  size_t html_len);
ir_node_t *ir_node_parent(const ir_node_t *node);

ir_root_t *ir_new_root(ir_t *ir);
ir_element_t *ir_new_element(ir_t *ir, const char *tag_name,
                             size_t tag_name_len, size_t source_start,
                             size_t source_end);
ir_text_t *ir_new_text(ir_t *ir, const char *text, size_t text_len);
ir_comment_t *ir_new_comment(ir_t *ir, const char *text, size_t text_len);

int ir_append_child(ir_node_t *parent, ir_node_t *child);
int ir_append_attr(ir_element_t *element, const char *name, size_t name_len,
                   const char *value, size_t value_len);

#endif /* REFLOW_IR_INTERNAL_H */
