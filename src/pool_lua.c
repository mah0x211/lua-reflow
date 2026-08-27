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

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <lua_errno.h>

#include "compat.h"
#include "pool.h"
#include "pool_internal.h"

#define POOL_METATABLE_NAME     "reflow.pool"
#define POOL_MEM_METATABLE_NAME "reflow.pool.mem"

static void *check_lightuserdata(lua_State *L, int idx)
{
    if (!lua_islightuserdata(L, idx)) {
        luaL_argerror(L, idx, "light userdata expected");
    }
    return lua_touserdata(L, idx);
}

/* Push a used block userdata and return its private header, or NULL on miss. */
static pool_mem_t *push_used_mem(lua_State *L, pool_t *pool, void *data)
{
    int state_idx   = 0;
    int used_idx    = 0;
    pool_mem_t *mem = NULL;

    pool_push_state(L, pool);
    state_idx = lua_gettop(L);
    lua_rawgeti(L, state_idx, POOL_STATE_USED_MEM_BY_BLOCK);
    used_idx = lua_gettop(L);
    lua_pushlightuserdata(L, data);
    lua_rawget(L, used_idx);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 3);
        return NULL;
    }

    mem = (pool_mem_t *)lua_touserdata(L, -1);
    lua_remove(L, used_idx);
    lua_remove(L, state_idx);
    return mem;
}

static int pool_gc_lua(lua_State *L)
{
    pool_t *pool = (pool_t *)lua_touserdata(L, 1);

    if (pool != NULL) {
        pool_delete(L, pool);
    }
    return 0;
}

static int pool_alloc_lua(lua_State *L)
{
    pool_t *pool     = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    lua_Integer size = luaL_checkinteger(L, 2);
    void *mem        = NULL;

    if (size < 0) {
        return luaL_argerror(L, 2, "size must be non-negative");
    }
    mem = pool_alloc(pool, (size_t)size);
    if (mem == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.alloc");
        return 2;
    }
    lua_pushlightuserdata(L, mem);
    return 1;
}

static int pool_aligned_alloc_lua(lua_State *L)
{
    pool_t *pool     = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    lua_Integer size = luaL_checkinteger(L, 2);
    lua_Integer alignment = luaL_checkinteger(L, 3);
    void *mem             = NULL;

    if (size < 0) {
        return luaL_argerror(L, 2, "size must be non-negative");
    } else if (alignment < 0) {
        return luaL_argerror(L, 3, "alignment must be non-negative");
    }
    mem = pool_aligned_alloc(pool, (size_t)size, (size_t)alignment);
    if (mem == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.aligned_alloc");
        return 2;
    }
    lua_pushlightuserdata(L, mem);
    return 1;
}

static int pool_calloc_lua(lua_State *L)
{
    pool_t *pool      = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    lua_Integer count = luaL_checkinteger(L, 2);
    lua_Integer size  = luaL_checkinteger(L, 3);
    void *mem         = NULL;

    if (count < 0) {
        return luaL_argerror(L, 2, "count must be non-negative");
    } else if (size < 0) {
        return luaL_argerror(L, 3, "size must be non-negative");
    }
    mem = pool_calloc(pool, (size_t)count, (size_t)size);
    if (mem == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.calloc");
        return 2;
    }
    lua_pushlightuserdata(L, mem);
    return 1;
}

static int pool_realloc_lua(lua_State *L)
{
    pool_t *pool     = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    void *mem        = NULL;
    lua_Integer size = 0;
    void *result     = NULL;

    if (!lua_isnoneornil(L, 2)) {
        mem = check_lightuserdata(L, 2);
    }
    size = luaL_checkinteger(L, 3);
    if (size < 0) {
        return luaL_argerror(L, 3, "size must be non-negative");
    }

    errno  = 0;
    result = pool_realloc(pool, mem, (size_t)size);
    if (result == NULL && !(mem != NULL && size == 0 && errno == 0)) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.realloc");
        return 2;
    }
    lua_pushlightuserdata(L, result);
    return 1;
}

static int pool_free_lua(lua_State *L)
{
    pool_t *pool = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    void *mem    = NULL;

    if (!lua_isnoneornil(L, 2)) {
        mem = check_lightuserdata(L, 2);
    }
    if (pool_free(pool, mem) != 0) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.free");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int pool_detach_lua(lua_State *L)
{
    pool_t *pool         = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    void *data           = check_lightuserdata(L, 2);
    pool_mem_t *detached = pool_detach(pool, data);
    int ref              = LUA_NOREF;

    if (detached == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.detach");
        return 2;
    }

    pool_mem_push(L, detached);
    luaL_getmetatable(L, POOL_MEM_METATABLE_NAME);
    lua_setmetatable(L, -2);

    /* Transfer registry ownership to the Lua value now on the stack. */
    ref           = detached->ref;
    detached->ref = LUA_NOREF;
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

static int pool_write_lua(lua_State *L)
{
    pool_t *pool       = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    void *data         = check_lightuserdata(L, 2);
    lua_Integer offset = luaL_checkinteger(L, 3);
    size_t len         = 0;
    const char *src    = NULL;
    pool_mem_t *mem    = NULL;

    if (offset < 0) {
        return luaL_argerror(L, 3, "offset must be non-negative");
    }
    src = luaL_checklstring(L, 4, &len);
    mem = push_used_mem(L, pool, data);
    if (mem == NULL || (size_t)offset > mem->size ||
        len > mem->size - (size_t)offset) {
        if (mem != NULL) {
            lua_pop(L, 1);
        }
        lua_pushnil(L);
        lua_errno_new(L, EINVAL, "reflow.pool.write");
        return 2;
    }
    memcpy((char *)mem->data + (size_t)offset, src, len);
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    return 1;
}

static int pool_read_lua(lua_State *L)
{
    pool_t *pool       = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    void *data         = check_lightuserdata(L, 2);
    lua_Integer offset = luaL_checkinteger(L, 3);
    lua_Integer size   = luaL_checkinteger(L, 4);
    pool_mem_t *mem    = NULL;

    if (offset < 0) {
        return luaL_argerror(L, 3, "offset must be non-negative");
    } else if (size < 0) {
        return luaL_argerror(L, 4, "size must be non-negative");
    }
    mem = push_used_mem(L, pool, data);
    if (mem == NULL || (size_t)offset > mem->size ||
        (size_t)size > mem->size - (size_t)offset) {
        if (mem != NULL) {
            lua_pop(L, 1);
        }
        lua_pushnil(L);
        lua_errno_new(L, EINVAL, "reflow.pool.read");
        return 2;
    }
    lua_pushlstring(L, (const char *)mem->data + (size_t)offset, (size_t)size);
    lua_remove(L, -2);
    return 1;
}

static int pool_is_aligned_lua(lua_State *L)
{
    pool_t *pool = (pool_t *)luaL_checkudata(L, 1, POOL_METATABLE_NAME);
    void *data   = check_lightuserdata(L, 2);
    lua_Integer alignment = luaL_checkinteger(L, 3);
    pool_mem_t *mem       = NULL;

    if (alignment <= 0 || ((size_t)alignment & ((size_t)alignment - 1)) != 0) {
        lua_pushnil(L);
        lua_errno_new(L, EINVAL, "reflow.pool.is_aligned");
        return 2;
    }
    mem = push_used_mem(L, pool, data);
    if (mem == NULL) {
        lua_pushnil(L);
        lua_errno_new(L, EINVAL, "reflow.pool.is_aligned");
        return 2;
    }
    lua_pop(L, 1);
    lua_pushboolean(L, ((uintptr_t)data % (size_t)alignment) == 0);
    return 1;
}

static int mem_size_lua(lua_State *L)
{
    pool_mem_t *mem =
        (pool_mem_t *)luaL_checkudata(L, 1, POOL_MEM_METATABLE_NAME);

    lua_pushinteger(L, (lua_Integer)pool_mem_size(mem));
    return 1;
}

static int mem_data_lua(lua_State *L)
{
    pool_mem_t *mem =
        (pool_mem_t *)luaL_checkudata(L, 1, POOL_MEM_METATABLE_NAME);

    lua_pushlightuserdata(L, pool_mem_data(mem));
    return 1;
}

static int new_lua(lua_State *L)
{
    lua_Integer capacity = luaL_optinteger(L, 1, 0);
    pool_t *pool         = NULL;

    if (capacity < 0) {
        return luaL_argerror(L, 1, "capacity must be non-negative");
    }
    pool = pool_new(L, (size_t)capacity);
    if (pool == NULL) {
        int errnum = errno;
        lua_pushnil(L);
        lua_errno_new(L, errnum, "reflow.pool.new");
        return 2;
    }
    luaL_getmetatable(L, POOL_METATABLE_NAME);
    lua_setmetatable(L, -2);
    return 1;
}

LUALIB_API int luaopen_reflow_pool(lua_State *L)
{
    static const luaL_Reg pool_methods[] = {
        {"alloc",         pool_alloc_lua        },
        {"aligned_alloc", pool_aligned_alloc_lua},
        {"calloc",        pool_calloc_lua       },
        {"realloc",       pool_realloc_lua      },
        {"free",          pool_free_lua         },
        {"detach",        pool_detach_lua       },
        {"write",         pool_write_lua        },
        {"read",          pool_read_lua         },
        {"is_aligned",    pool_is_aligned_lua   },
        {NULL,            NULL                  },
    };
    static const luaL_Reg mem_methods[] = {
        {"size", mem_size_lua},
        {"data", mem_data_lua},
        {NULL,   NULL        },
    };
    static const luaL_Reg module_funcs[] = {
        {"new", new_lua},
        {NULL,  NULL   },
    };

    lua_errno_loadlib(L);

    if (luaL_newmetatable(L, POOL_METATABLE_NAME)) {
        lua_pushcfunction(L, pool_gc_lua);
        lua_setfield(L, -2, "__gc");
        lua_pushliteral(L, POOL_METATABLE_NAME);
        lua_setfield(L, -2, "__metatable");
        luaL_newlib(L, pool_methods);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, POOL_MEM_METATABLE_NAME)) {
        lua_pushliteral(L, POOL_MEM_METATABLE_NAME);
        lua_setfield(L, -2, "__metatable");
        luaL_newlib(L, mem_methods);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    luaL_newlib(L, module_funcs);
    return 1;
}
