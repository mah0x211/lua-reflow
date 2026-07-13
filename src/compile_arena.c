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
/*
 * Implementation notes:
 *
 * lua_newuserdata is called on L (the main state with a pcall frame),
 * NOT on a->LT.  If called on a->LT (which has no pcall frame), an OOM
 * throw would bypass every protected call and abort the process.
 * After allocation, lua_xmove transfers the chunk from L's stack to
 * a->LT's stack for GC-tracked storage.
 */

// project
#include "compile_arena.h"
// lua
#include <lauxlib.h>
// system
#include <string.h>

#define DEFAULT_CHUNK_SIZE 4096
#define ALIGN_MASK         ((size_t)7)

/* ── __gc metamethod ──────────────────────────────────────── */

static int arena_gc(lua_State *L)
{
    compile_arena *a = (compile_arena *)lua_touserdata(L, 1);
    if (a && a->thread_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, a->thread_ref);
        a->thread_ref = LUA_NOREF;
        a->LT         = NULL;
    }
    return 0;
}

/* Ensure the metatable exists in the registry. */
static void ensure_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, "reflow.compile_arena")) {
        lua_pushcfunction(L, arena_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);
}

/* ── public API ───────────────────────────────────────────── */

compile_arena *compile_arena_new(lua_State *L, size_t chunk_size)
{
    if (chunk_size == 0) chunk_size = DEFAULT_CHUNK_SIZE;

    ensure_metatable(L);

    /* 1. Arena struct as userdata */
    compile_arena *a = (compile_arena *)lua_newuserdata(L, sizeof(*a));
    memset(a, 0, sizeof(*a));
    luaL_getmetatable(L, "reflow.compile_arena");
    lua_setmetatable(L, -2);

    /* 2. Thread for chunk storage */
    lua_State *LT = lua_newthread(L);
    a->thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    a->LT         = LT;
    a->chunk_size = chunk_size;
    a->current    = NULL;
    a->remaining  = 0;

    return a;
}

void *compile_arena_alloc(compile_arena *a, lua_State *L, size_t n)
{
    /* Align to 8 bytes */
    n = (n + ALIGN_MASK) & ~ALIGN_MASK;
    if (n == 0) n = ALIGN_MASK + 1;

    if (n > a->remaining) {
        size_t alloc_size = a->chunk_size;
        if (n > alloc_size) alloc_size = n;

        /* Allocate on L (has pcall frame), then move to LT */
        char *chunk = (char *)lua_newuserdata(L, alloc_size);

        if (!lua_checkstack(a->LT, 1)) {
            lua_pop(L, 1);   /* remove chunk from L's stack */
            return NULL;      /* cannot grow LT's stack */
        }
        lua_xmove(L, a->LT, 1);   /* move chunk onto LT's stack */

        a->current   = chunk;
        a->remaining = alloc_size;
    }

    void *ptr    = a->current;
    a->current  += n;
    a->remaining -= n;
    return ptr;
}
