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
#include "checked.h"
// lua
#include <lauxlib.h>
// system
#include <string.h>

#define DEFAULT_CHUNK_SIZE 4096
#define ARENA_ALIGNMENT    ((size_t)16)

/* Ensure the metatable exists in the registry. */
static void ensure_metatable(lua_State *L)
{
    luaL_newmetatable(L, "reflow.compile_arena");
    lua_pop(L, 1);
}

static void set_private_owner(lua_State *L, int owner_index)
{
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
    lua_setuservalue(L, owner_index);
#else
    lua_setfenv(L, owner_index);
#endif
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

    /* The private table is the only ownership edge needed: arena -> thread.
     * Both values remain stack-rooted throughout construction, so an error at
     * any throwing step cannot strand a manual registry reference. */
    lua_newtable(L);
    lua_State *LT = lua_newthread(L);
    lua_rawseti(L, -2, 1);
    set_private_owner(L, -2);
    a->LT         = LT;
    a->chunk_size = chunk_size;
    a->current    = NULL;
    a->remaining  = 0;

    return a;
}

void *compile_arena_alloc(compile_arena *a, lua_State *L, size_t n)
{
    size_t aligned = 0;
    if (!reflow_size_align(n == 0 ? 1 : n, ARENA_ALIGNMENT, &aligned)) {
        luaL_error(L, "reflow arena allocation size overflow");
    }
    n = aligned;

    if (n > a->remaining) {
        size_t alloc_size = a->chunk_size;
        if (n > alloc_size) {
            reflow_size_grow(alloc_size, n, &alloc_size);
        }

        /* Reserve the non-throwing transfer destination before allocation.
         * The allocation itself must occur on protected L, never on LT. */
        if (!lua_checkstack(a->LT, 1)) {
            luaL_error(L, "reflow arena child stack exhausted");
        }
        char *chunk = (char *)lua_newuserdata(L, alloc_size);
        memset(chunk, 0, alloc_size);
        lua_xmove(L, a->LT, 1);

        a->current   = chunk;
        a->remaining = alloc_size;
        a->chunk_size = alloc_size;
    }

    void *ptr    = a->current;
    a->current  += n;
    a->remaining -= n;
    return ptr;
}
