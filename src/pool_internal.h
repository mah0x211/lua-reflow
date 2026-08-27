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

/*
 * Private interface shared between pool.c and its Lua test bridge pool_lua.c.
 * These symbols are intentionally kept out of the public pool.h; they exist
 * only so the bridge can inspect internal state for testing and diagnostics.
 */

#ifndef REFLOW_POOL_INTERNAL_H
#define REFLOW_POOL_INTERNAL_H

#include <lua.h>
#include <stddef.h>

#include "pool.h"

/**
 * @brief Slot keys of the pool's internal state table.
 *
 * Mirrors the private enum in pool.c so the bridge and tests can read specific
 * internal tables without depending on their layout by number.
 */
enum pool_state_slot {
    POOL_STATE_USED_MEM_BY_BLOCK = 1, /* data pointer -> block (strong) */
    POOL_STATE_UNUSED_MEM_BY_SIZE,    /* size key -> chain head (weak value) */
    POOL_STATE_UNUSED_MEM_NEXT        /* block -> next block (weak kv) */
};

/** Private block header shared with the Lua bridge. */
struct pool_mem_t {
    pool_t *pool;
    size_t size;
    size_t alignment;
    int ref;
    void *data;
};

/**
 * @brief Push the pool's internal state table onto the Lua stack.
 *
 * For testing and diagnostics only.  The pool must not have been deleted.
 */
void pool_push_state(lua_State *L, pool_t *pool);

/** Push the userdata held by a detached block's registry reference. */
void pool_mem_push(lua_State *L, pool_mem_t *mem);

#endif /* REFLOW_POOL_INTERNAL_H */
