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

#ifndef REFLOW_ERROR_H
#define REFLOW_ERROR_H

#include <lauxlib.h>
#include <lua.h>

static inline int reflow_error_absolute_index(lua_State *L, int idx)
{
    if (idx > 0 || idx <= LUA_REGISTRYINDEX) {
        return idx;
    }
    return lua_gettop(L) + idx + 1;
}

/** Push require(module_name), raising the original Lua error on failure. */
static inline void reflow_error_require(lua_State *L, const char *module_name)
{
    lua_getglobal(L, "require");
    lua_pushstring(L, module_name);
    lua_call(L, 1, 1);
}

/**
 * Push reflow.error.new_compile(op, message, cause, meta).
 *
 * cause_idx and meta_idx are existing stack indices, or 0 to pass nil.  The
 * named Lua operation owns all error types and representation details; this
 * helper only preserves its stack contract.
 */
static inline void reflow_error_push_compile(lua_State *L, const char *op,
                                             const char *message, int cause_idx,
                                             int meta_idx)
{
    int cause = cause_idx == 0 ? 0 : reflow_error_absolute_index(L, cause_idx);
    int meta  = meta_idx == 0 ? 0 : reflow_error_absolute_index(L, meta_idx);

    reflow_error_require(L, "reflow.error");
    lua_getfield(L, -1, "new_compile");
    lua_remove(L, -2);
    lua_pushstring(L, op);
    lua_pushstring(L, message);
    if (cause == 0) {
        lua_pushnil(L);
    } else {
        lua_pushvalue(L, cause);
    }
    if (meta == 0) {
        lua_pushnil(L);
    } else {
        lua_pushvalue(L, meta);
    }
    lua_call(L, 4, 1);
}

#endif /* REFLOW_ERROR_H */
