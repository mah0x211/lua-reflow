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

/* Lua 5.1 does not define LUA_OK; the success thread status is 0 there. */
#ifndef LUA_OK
# define LUA_OK 0
#endif

/* Lua 5.1 lacks luaL_newlib; register the functions into a fresh table. */
#ifndef luaL_newlib
# define luaL_newlib(L, l) (lua_newtable((L)), luaL_register((L), NULL, (l)))
#endif

/* Store the owner table attached to full userdata across Lua 5.1-5.4. */
#if LUA_VERSION_NUM == 501
# define reflow_getuservalue(L, idx) lua_getfenv((L), (idx))
# define reflow_setuservalue(L, idx) lua_setfenv((L), (idx))
#elif LUA_VERSION_NUM < 504
# define reflow_getuservalue(L, idx) lua_getuservalue((L), (idx))
# define reflow_setuservalue(L, idx) lua_setuservalue((L), (idx))
#else
# define reflow_getuservalue(L, idx) lua_getiuservalue((L), (idx), 1)
# define reflow_setuservalue(L, idx) lua_setiuservalue((L), (idx), 1)
#endif

#endif /* REFLOW_COMPAT_H */
