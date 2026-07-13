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

// project
#include "ir.h"
// lua
#include <lua.h>
// system
#include <string.h>

/* ── Factory functions ────────────────────────────────────── */

ir_node *ir_make_root(compile_arena *arena, lua_State *L)
{
    ir_node *n = (ir_node *)compile_arena_alloc(arena, L, sizeof(ir_node));
    memset(n, 0, sizeof(*n));
    n->type             = IR_ROOT;
    n->root.children    = NULL;
    n->root.n_children  = 0;
    return n;
}

ir_node *ir_make_element(compile_arena *arena, lua_State *L,
                         char *tag_name,
                         size_t source_start, size_t source_end)
{
    ir_node *n = (ir_node *)compile_arena_alloc(arena, L, sizeof(ir_node));
    memset(n, 0, sizeof(*n));
    n->type                  = IR_ELEMENT;
    n->element.tag_name      = tag_name;
    n->element.attrs         = NULL;
    n->element.n_attrs       = 0;
    n->element.children      = NULL;
    n->element.n_children    = 0;
    n->element.source_start  = source_start;
    n->element.source_end    = source_end;
    n->element.invisible_marker = false;
    return n;
}

ir_node *ir_make_text(compile_arena *arena, lua_State *L,
                      char *text, size_t text_len)
{
    ir_node *n = (ir_node *)compile_arena_alloc(arena, L, sizeof(ir_node));
    memset(n, 0, sizeof(*n));
    n->type         = IR_TEXT;
    n->text.text    = text;
    n->text.text_len = text_len;
    return n;
}

ir_node *ir_make_comment(compile_arena *arena, lua_State *L,
                         char *text, size_t text_len)
{
    ir_node *n = (ir_node *)compile_arena_alloc(arena, L, sizeof(ir_node));
    memset(n, 0, sizeof(*n));
    n->type            = IR_COMMENT;
    n->comment.text    = text;
    n->comment.text_len = text_len;
    return n;
}

ir_node *ir_make_chain(compile_arena *arena, lua_State *L,
                       ir_branch *branches, size_t n_branches,
                       size_t source_start, size_t source_end)
{
    ir_node *n = (ir_node *)compile_arena_alloc(arena, L, sizeof(ir_node));
    memset(n, 0, sizeof(*n));
    n->type               = IR_CHAIN;
    n->chain.branches     = branches;
    n->chain.n_branches   = n_branches;
    n->chain.source_start = source_start;
    n->chain.source_end   = source_end;
    return n;
}

/* ── Child / attribute management ─────────────────────────── */

void ir_add_child(compile_arena *arena, lua_State *L,
                  ir_node *parent, ir_node *child)
{
    ir_node ***childrenp;
    size_t *np;

    if (parent->type == IR_ROOT) {
        childrenp = &parent->root.children;
        np        = &parent->root.n_children;
    } else {
        childrenp = &parent->element.children;
        np        = &parent->element.n_children;
    }

    /* Grow: allocate new array with doubled capacity, copy, replace */
    size_t old_n = *np;
    size_t new_cap = old_n + 1;
    /* Round up to power-of-2-ish for amortized growth */
    if (old_n > 0) new_cap = old_n * 2;

    ir_node **ni = (ir_node **)compile_arena_alloc(
        arena, L, new_cap * sizeof(ir_node *));
    if (old_n > 0)
        memcpy(ni, *childrenp, old_n * sizeof(ir_node *));
    *childrenp = ni;
    (*childrenp)[old_n] = child;
    *np = old_n + 1;
}

void ir_add_attr(compile_arena *arena, lua_State *L,
                 ir_node *element, const char *name, const char *value)
{
    size_t old_n = element->element.n_attrs;
    size_t new_cap = old_n + 1;
    if (old_n > 0) new_cap = old_n * 2;

    ir_attr *na = (ir_attr *)compile_arena_alloc(
        arena, L, new_cap * sizeof(ir_attr));
    if (old_n > 0)
        memcpy(na, element->element.attrs, old_n * sizeof(ir_attr));
    element->element.attrs = na;
    na[old_n].name  = name;
    na[old_n].value = value;
    element->element.n_attrs = old_n + 1;
}

/* ── Helpers ──────────────────────────────────────────────── */

bool ir_is_ignorable(const ir_node *node)
{
    if (node->type == IR_COMMENT)
        return true;
    if (node->type == IR_TEXT) {
        for (size_t i = 0; i < node->text.text_len; i++) {
            char c = node->text.text[i];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                return false;
        }
        return true;
    }
    return false;
}
