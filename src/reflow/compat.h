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

#ifndef REFLOW_COMPAT_H
#define REFLOW_COMPAT_H

#include <lauxlib.h>
#include <lua.h>

#if LUA_VERSION_NUM == 501
# define REFLOW_LUA_OK     0
# define reflow_lua_rawlen lua_objlen
# define luaL_newlib(L, l) (lua_newtable((L)), luaL_register((L), NULL, (l)))
#else
# define REFLOW_LUA_OK     LUA_OK
# define reflow_lua_rawlen lua_rawlen
#endif

#define REFLOW_LUA_VERSION_NUMBER LUA_VERSION_NUM

#endif /* REFLOW_COMPAT_H */
