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
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */
#include "lua_selector.h"
#include "compile_arena.h"
#include "error.h"
#include <lauxlib.h>

reflow_selector *reflow_selector_check(lua_State *L, int idx)
{
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
    return (reflow_selector *)luaL_testudata(L, idx, REFLOW_SELECTOR_MT);
#else
    if (lua_type(L, idx) != LUA_TUSERDATA) return NULL;
    if (!lua_getmetatable(L, idx)) return NULL;
    luaL_getmetatable(L, REFLOW_SELECTOR_MT);
    int eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return eq ? (reflow_selector *)lua_touserdata(L, idx) : NULL;
#endif
}

static int selector_gc(lua_State *L)
{
    reflow_selector *s = (reflow_selector *)luaL_checkudata(
        L, 1, REFLOW_SELECTOR_MT);
    if (s->arena_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->arena_ref);
        s->arena_ref = LUA_NOREF;
    }
    return 0;
}

static int selector_source(lua_State *L)
{
    reflow_selector *s = (reflow_selector *)luaL_checkudata(
        L, 1, REFLOW_SELECTOR_MT);
    lua_pushlstring(L, s->sel->source, s->sel->source_len);
    return 1;
}

static int selector_has_positional(lua_State *L)
{
    reflow_selector *s = (reflow_selector *)luaL_checkudata(
        L, 1, REFLOW_SELECTOR_MT);
    lua_pushboolean(L, s->sel->has_positional);
    return 1;
}

/* new_selector(source) → selector_ud, nil | nil, err_table */
static int selector_new(lua_State *L)
{
    size_t slen = 0;
    const char *src = luaL_checklstring(L, 1, &slen);

    compile_arena *carena = compile_arena_new(L, 1024);
    int arena_stack_pos = lua_gettop(L);
    reflow_error err = {0};
    sel_compiled *sel = selector_parse(carena, L, src, slen, &err);
    if (sel == NULL) {
        lua_settop(L, arena_stack_pos);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushstring(L, err.type ? err.type : "ReflowSelectorError");
        lua_setfield(L, -2, "type");
        lua_pushstring(L, err.message ? err.message : "selector parse error");
        lua_setfield(L, -2, "message");
        if (err.reason)   { lua_pushstring(L, err.reason);   lua_setfield(L, -2, "reason"); }
        if (err.feature)  { lua_pushstring(L, err.feature);  lua_setfield(L, -2, "feature"); }
        if (err.source)   { lua_pushstring(L, err.source);   lua_setfield(L, -2, "source"); }
        if (err.position > 0) {
            lua_pushinteger(L, (lua_Integer)err.position);
            lua_setfield(L, -2, "position");
        }
        if (err.line > 0)   { lua_pushinteger(L, (lua_Integer)err.line);   lua_setfield(L, -2, "line"); }
        if (err.column > 0) { lua_pushinteger(L, (lua_Integer)err.column); lua_setfield(L, -2, "column"); }
        luaL_getmetatable(L, "reflow.error");
        if (!lua_isnil(L, -1)) lua_setmetatable(L, -2);
        else                    lua_pop(L, 1);
        return 2;
    }

    lua_pushvalue(L, arena_stack_pos);
    int arena_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_remove(L, arena_stack_pos);

    reflow_selector_register(L);
    reflow_selector *s = (reflow_selector *)lua_newuserdata(L, sizeof(*s));
    s->arena_ref = arena_ref;
    s->sel       = sel;
    luaL_getmetatable(L, REFLOW_SELECTOR_MT);
    lua_setmetatable(L, -2);
    return 1;
}

void reflow_selector_register(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_SELECTOR_MT)) {
        lua_pushcfunction(L, selector_gc);
        lua_setfield(L, -2, "__gc");
        lua_newtable(L);
        lua_pushcfunction(L, selector_source);
        lua_setfield(L, -2, "source");
        lua_pushcfunction(L, selector_has_positional);
        lua_setfield(L, -2, "has_positional");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int luaopen_reflow_selector(lua_State *L)
{
    reflow_selector_register(L);
    lua_pushcfunction(L, selector_new);
    return 1;
}
