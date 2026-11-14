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

/* Lua bridge of the arena for tests: reflow.arena maps the public C
 * operations of arena.h to Lua and adds byte helpers.  The product does not
 * use it. */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <lua_errno.h>

#include "arena.h"
#include "compat.h"

#define ARENA_METATABLE_NAME "reflow.arena"

static arena_t *check_arena(lua_State *L)
{
    return (arena_t *)luaL_checkudata(L, 1, ARENA_METATABLE_NAME);
}

static void *check_pointer(lua_State *L, int idx)
{
    if (!lua_islightuserdata(L, idx) || lua_touserdata(L, idx) == NULL) {
        luaL_argerror(L, idx, "non-NULL light userdata expected");
    }
    return lua_touserdata(L, idx);
}

static size_t check_size(lua_State *L, int idx, const char *name)
{
    lua_Integer v = luaL_checkinteger(L, idx);

    if (v < 0) {
        luaL_argerror(L, idx, name);
    }
    return (size_t)v;
}

/* Push the result of an operation that returns a pointer. */
static int push_pointer(lua_State *L, void *mem, const char *op)
{
    if (mem == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, op);
        return 2;
    }
    lua_pushlightuserdata(L, mem);
    return 1;
}

static int arena_gc_lua(lua_State *L)
{
    arena_t *arena = (arena_t *)lua_touserdata(L, 1);

    if (arena != NULL) {
        arena_delete(L, arena);
    }
    return 0;
}

static int arena_alloc_lua(lua_State *L)
{
    arena_t *arena    = check_arena(L);
    size_t size       = check_size(L, 2, "size must be non-negative");
    lua_Integer align = luaL_optinteger(L, 3, 0);

    if (align < 0) {
        return luaL_argerror(L, 3, "alignment must be non-negative");
    }
    return push_pointer(L, arena_alloc(arena, size, (size_t)align),
                        "reflow.arena.alloc");
}

static int arena_large_alloc_lua(lua_State *L)
{
    arena_t *arena = check_arena(L);
    size_t size    = check_size(L, 2, "size must be non-negative");

    return push_pointer(L, arena_large_alloc(arena, size),
                        "reflow.arena.large_alloc");
}

static int arena_large_free_lua(lua_State *L)
{
    arena_large_free(check_arena(L), check_pointer(L, 2));
    return 0;
}

static int arena_chunk_size_lua(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)arena_chunk_size(check_arena(L)));
    return 1;
}

static int arena_chunk_acquire_lua(lua_State *L)
{
    return push_pointer(L, arena_chunk_acquire(check_arena(L)),
                        "reflow.arena.chunk_acquire");
}

static int arena_chunk_release_lua(lua_State *L)
{
    arena_chunk_release(check_arena(L), check_pointer(L, 2));
    return 0;
}

static int arena_reset_lua(lua_State *L)
{
    arena_reset(check_arena(L));
    return 0;
}

static int arena_compact_lua(lua_State *L)
{
    arena_t *arena = check_arena(L);
    size_t size    = check_size(L, 2, "size must be non-negative");

    lua_pushinteger(L, (lua_Integer)arena_compact(arena, size));
    return 1;
}

static int arena_retained_lua(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)arena_retained(check_arena(L)));
    return 1;
}

/* Test helper: copy a string to ptr + offset.  The range is not checked. */
static int arena_write_lua(lua_State *L)
{
    void *mem     = NULL;
    size_t offset = 0;
    size_t len    = 0;
    const char *s = NULL;

    check_arena(L);
    mem    = check_pointer(L, 2);
    offset = check_size(L, 3, "offset must be non-negative");
    s      = luaL_checklstring(L, 4, &len);
    memcpy((char *)mem + offset, s, len);
    return 0;
}

/* Test helper: read size bytes at ptr + offset.  The range is not checked. */
static int arena_read_lua(lua_State *L)
{
    void *mem     = NULL;
    size_t offset = 0;
    size_t size   = 0;

    check_arena(L);
    mem    = check_pointer(L, 2);
    offset = check_size(L, 3, "offset must be non-negative");
    size   = check_size(L, 4, "size must be non-negative");
    lua_pushlstring(L, (const char *)mem + offset, size);
    return 1;
}

static int arena_is_aligned_lua(lua_State *L)
{
    void *mem         = NULL;
    lua_Integer align = 0;

    check_arena(L);
    mem   = check_pointer(L, 2);
    align = luaL_checkinteger(L, 3);
    if (align <= 0 || ((size_t)align & ((size_t)align - 1)) != 0) {
        return luaL_argerror(L, 3, "alignment must be a power of two");
    }
    lua_pushboolean(L, ((uintptr_t)mem % (size_t)align) == 0);
    return 1;
}

static int new_lua(lua_State *L)
{
    size_t chunk_size = 0;
    arena_t *arena    = NULL;

    if (!lua_isnoneornil(L, 1)) {
        chunk_size = check_size(L, 1, "chunk size must be non-negative");
    }
    arena = arena_new(L, chunk_size);
    if (arena == NULL) {
        return push_pointer(L, NULL, "reflow.arena.new");
    }
    luaL_getmetatable(L, ARENA_METATABLE_NAME);
    lua_setmetatable(L, -2);
    return 1;
}

static int page_size_lua(lua_State *L)
{
    long size = sysconf(_SC_PAGESIZE);

    lua_pushinteger(L, (lua_Integer)(size > 0 ? size : 4096));
    return 1;
}

LUALIB_API int luaopen_reflow_arena(lua_State *L)
{
    static const luaL_Reg methods[] = {
        {"alloc",         arena_alloc_lua        },
        {"large_alloc",   arena_large_alloc_lua  },
        {"large_free",    arena_large_free_lua   },
        {"chunk_size",    arena_chunk_size_lua   },
        {"chunk_acquire", arena_chunk_acquire_lua},
        {"chunk_release", arena_chunk_release_lua},
        {"reset",         arena_reset_lua        },
        {"compact",       arena_compact_lua      },
        {"retained",      arena_retained_lua     },
        {"write",         arena_write_lua        },
        {"read",          arena_read_lua         },
        {"is_aligned",    arena_is_aligned_lua   },
        {NULL,            NULL                   },
    };
    static const luaL_Reg funcs[] = {
        {"new",       new_lua      },
        {"page_size", page_size_lua},
        {NULL,        NULL         },
    };

    lua_errno_loadlib(L);
    if (luaL_newmetatable(L, ARENA_METATABLE_NAME)) {
        lua_pushcfunction(L, arena_gc_lua);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, ARENA_METATABLE_NAME);
        lua_setfield(L, -2, "__metatable");
        luaL_newlib(L, methods);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
