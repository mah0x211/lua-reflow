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

/* Lua bridge of the slab for tests: reflow.slab maps the public C operations
 * of slab.h to Lua and adds byte helpers.  Each slab owns an arena of its
 * own, so that no C structure is shared with another artifact.  The product
 * does not use it. */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <lua_errno.h>

#include "arena.h"
#include "compat.h"
#include "slab.h"

#define SLAB_METATABLE_NAME "reflow.slab"

typedef struct {
    slab_t slab;
    arena_t *arena;
} slab_box_t;

static slab_box_t *check_box(lua_State *L)
{
    return (slab_box_t *)luaL_checkudata(L, 1, SLAB_METATABLE_NAME);
}

static void *check_pointer(lua_State *L, int idx)
{
    if (!lua_islightuserdata(L, idx) || lua_touserdata(L, idx) == NULL) {
        luaL_argerror(L, idx, "non-NULL light userdata expected");
    }
    return lua_touserdata(L, idx);
}

static size_t check_size(lua_State *L, int idx, const char *message)
{
    lua_Integer v = luaL_checkinteger(L, idx);

    if (v < 0) {
        luaL_argerror(L, idx, message);
    }
    return (size_t)v;
}

static int slab_gc_lua(lua_State *L)
{
    slab_box_t *box = (slab_box_t *)lua_touserdata(L, 1);

    if (box != NULL && box->arena != NULL) {
        arena_delete(L, box->arena);
        box->arena = NULL;
    }
    return 0;
}

static int slab_alloc_lua(lua_State *L)
{
    void *object = slab_alloc(&check_box(L)->slab);

    if (object == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.slab.alloc");
        return 2;
    }
    lua_pushlightuserdata(L, object);
    return 1;
}

static int slab_free_lua(lua_State *L)
{
    slab_free(&check_box(L)->slab, check_pointer(L, 2));
    return 0;
}

static int slab_reset_lua(lua_State *L)
{
    slab_reset(&check_box(L)->slab);
    return 0;
}

static int slab_object_size_lua(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)check_box(L)->slab.object_size);
    return 1;
}

static int slab_retained_lua(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)arena_retained(check_box(L)->arena));
    return 1;
}

/* Test helper: copy a string to ptr + offset.  The range is not checked. */
static int slab_write_lua(lua_State *L)
{
    void *mem     = NULL;
    size_t offset = 0;
    size_t len    = 0;
    const char *s = NULL;

    check_box(L);
    mem    = check_pointer(L, 2);
    offset = check_size(L, 3, "offset must be non-negative");
    s      = luaL_checklstring(L, 4, &len);
    memcpy((char *)mem + offset, s, len);
    return 0;
}

/* Test helper: read size bytes at ptr + offset.  The range is not checked. */
static int slab_read_lua(lua_State *L)
{
    void *mem     = NULL;
    size_t offset = 0;
    size_t size   = 0;

    check_box(L);
    mem    = check_pointer(L, 2);
    offset = check_size(L, 3, "offset must be non-negative");
    size   = check_size(L, 4, "size must be non-negative");
    lua_pushlstring(L, (const char *)mem + offset, size);
    return 1;
}

static int slab_is_aligned_lua(lua_State *L)
{
    void *mem         = NULL;
    lua_Integer align = 0;

    check_box(L);
    mem   = check_pointer(L, 2);
    align = luaL_checkinteger(L, 3);
    if (align <= 0 || ((size_t)align & ((size_t)align - 1)) != 0) {
        return luaL_argerror(L, 3, "alignment must be a power of two");
    }
    lua_pushboolean(L, ((uintptr_t)mem % (size_t)align) == 0);
    return 1;
}

/* new(object_size [, chunk_size]) */
static int new_lua(lua_State *L)
{
    size_t object_size = check_size(L, 1, "object size must be non-negative");
    size_t chunk_size  = 0;
    slab_box_t *box    = NULL;

    if (!lua_isnoneornil(L, 2)) {
        chunk_size = check_size(L, 2, "chunk size must be non-negative");
    }
    lua_settop(L, 2);
    box  = (slab_box_t *)lua_newuserdata(L, sizeof(*box));
    *box = (slab_box_t){0};
    luaL_getmetatable(L, SLAB_METATABLE_NAME);
    lua_setmetatable(L, -2);
    lua_createtable(L, 1, 0);
    box->arena = arena_new(L, chunk_size);
    if (box->arena == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.slab.new");
        return 2;
    }
    /* The slab keeps its arena in its owner table. */
    lua_rawseti(L, -2, 1);
    reflow_setuservalue(L, -2);
    if (slab_init(&box->slab, box->arena, object_size) != 0) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.slab.new");
        return 2;
    }
    return 1;
}

LUALIB_API int luaopen_reflow_slab(lua_State *L)
{
    static const luaL_Reg methods[] = {
        {"alloc",       slab_alloc_lua      },
        {"free",        slab_free_lua       },
        {"reset",       slab_reset_lua      },
        {"object_size", slab_object_size_lua},
        {"retained",    slab_retained_lua   },
        {"write",       slab_write_lua      },
        {"read",        slab_read_lua       },
        {"is_aligned",  slab_is_aligned_lua },
        {NULL,          NULL                },
    };
    static const luaL_Reg funcs[] = {
        {"new", new_lua},
        {NULL,  NULL   },
    };

    lua_errno_loadlib(L);
    if (luaL_newmetatable(L, SLAB_METATABLE_NAME)) {
        lua_pushcfunction(L, slab_gc_lua);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, SLAB_METATABLE_NAME);
        lua_setfield(L, -2, "__metatable");
        luaL_newlib(L, methods);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
