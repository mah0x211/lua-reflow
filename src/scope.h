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

#ifndef REFLOW_SCOPE_H
#define REFLOW_SCOPE_H

#include <stddef.h>

#include <lua.h>

#include "pool.h"

typedef enum scope_kind_t {
    SCOPE_DATA = 1, /* x-data, x-with, and the bindings of an include */
    SCOPE_LOOP      /* x-for and x-each */
} scope_kind_t;

/* The lexical scope of one render. The globals table ($) and a table holding
 * the frames by depth live at fixed positions on the Lua stack; only the
 * frame kinds are kept in C. Each frame is a table whose keys are the names
 * it binds. */
typedef struct scope_t {
    lua_State *L;
    pool_t *pool;
    int globals_idx;
    int frames_idx;
    unsigned char *kinds;
    size_t depth;
    size_t capacity;
} scope_t;

void scope_init(scope_t *scope, lua_State *L, pool_t *pool, int globals_idx,
                int frames_idx);

/**
 * Pop the table on top of the stack and push it as the innermost frame.
 *
 * @return 0, or -1 when the pool runs out of memory; the table is popped
 *         either way.
 */
int scope_push(scope_t *scope, scope_kind_t kind);

/* Drop the frames above depth. */
void scope_pop_to(scope_t *scope, size_t depth);

/* Push the globals table ($). */
void scope_push_dollar(const scope_t *scope);

/* Push the value bound to name by the innermost data frame that has the key
 * (@name), or nil. */
void scope_push_at(const scope_t *scope, const char *name, size_t len);

/* Push the value bound to name by the innermost frame of any kind that has
 * the key (.name), or nil. */
void scope_push_dot(const scope_t *scope, const char *name, size_t len);

#endif /* REFLOW_SCOPE_H */
