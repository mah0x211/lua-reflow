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
#ifndef REFLOW_COMPILE_ARENA_H
#define REFLOW_COMPILE_ARENA_H

#include <lua.h>
#include <stddef.h>

/*
 * Growable bump-allocation arena backed by lua_newuserdata chunks.
 *
 * Memory is allocated as GC-tracked userdata chunks and stored on a
 * dedicated lua_newthread's stack.  When the arena userdata is collected
 * by GC, __gc releases the thread reference (luaL_unref); the thread
 * becomes unreachable, its stack is cleared, and every chunk on it is
 * collected — all through Lua's normal GC cycle.
 *
 * Design guarantees:
 *   - All memory is GC-visible (incremental GC, memory caps work)
 *   - longjmp-safe (every reference is Lua-managed — no C-owned malloc)
 *   - OOM is propagated via lua_newuserdata (throw to nearest pcall)
 *   - No per-allocation free (bump allocate; bulk GC cleanup)
 */

typedef struct compile_arena {
    int       thread_ref;   /* luaL_ref in registry (LUA_NOREF after __gc) */
    lua_State *LT;          /* cached thread state for stack storage */
    char     *current;      /* bump pointer into current chunk */
    size_t    remaining;    /* bytes left in current chunk */
    size_t    chunk_size;   /* size of each internal chunk */
} compile_arena;

/*
 * Create a new arena as a lua_newuserdata on L's stack.
 * The userdata has a __gc metamethod that releases the thread ref.
 * chunk_size is the internal chunk size (0 = default 4096).
 *
 * On success: arena userdata is on top of L's stack; returns arena pointer.
 * On OOM: lua_newuserdata / lua_newthread throws (longjmp to pcall).
 */
compile_arena *compile_arena_new(lua_State *L, size_t chunk_size);

/*
 * Bump-allocate `n` bytes (aligned to 8) from the arena.
 * If the current chunk is full, a new chunk is allocated via
 * lua_newuserdata on L (which has the pcall frame), then moved to
 * the thread's stack via lua_xmove.
 *
 * Returns pointer to memory, or NULL if lua_checkstack(LT) fails.
 * On OOM: lua_newuserdata throws (longjmp to nearest pcall on L).
 */
void *compile_arena_alloc(compile_arena *a, lua_State *L, size_t n);

#endif /* REFLOW_COMPILE_ARENA_H */
