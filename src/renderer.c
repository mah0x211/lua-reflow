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
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * reflow instance state and lifecycle.
 *
 * Templates and helpers live in Lua-registry tables held by
 * reflow_state via luaL_ref; releasing those refs in __gc lets the
 * garbage collector reclaim every compile_arena and helper function
 * associated with the instance. There is no C-owned memory besides
 * the userdata itself.
 */

// project
#include "renderer.h"
#include "arena.h"
#include "buf.h"
#include "compile.h"
#include "compile_arena.h"
#include "error.h"
#include "expr/eval.h"
#include "interpret.h"
#include "ir.h"
#include "json5.h"
// system
#include <string.h>

#define REFLOW_STATE_MT  "reflow.state"

typedef struct reflow_state {
    int    templates_ref;   /* Lua table {name -> template entry} */
    int    helpers_ref;     /* Lua table {name -> function} */
    int    helper_names_ref; /* Lua table {name -> true} (compile static) */
    char   prefix[16];
    size_t prefix_len;
    int    max_include_depth;
} reflow_state;

static reflow_state *check_state(lua_State *L, int idx)
{
    return (reflow_state *)luaL_checkudata(L, idx, REFLOW_STATE_MT);
}

static int state_gc(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    if (s->templates_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->templates_ref);
        s->templates_ref = LUA_NOREF;
    }
    if (s->helpers_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->helpers_ref);
        s->helpers_ref = LUA_NOREF;
    }
    if (s->helper_names_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->helper_names_ref);
        s->helper_names_ref = LUA_NOREF;
    }
    return 0;
}

static void ensure_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_STATE_MT)) {
        lua_pushcfunction(L, state_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);
}

int rf_state_new(lua_State *L)
{
    const char *prefix = luaL_optstring(L, 1, "x-");
    int max_include_depth = (int)luaL_optinteger(L, 2, 50);

    size_t plen = strlen(prefix);
    if (plen == 0 || plen >= sizeof(((reflow_state *)0)->prefix)) {
        return luaL_error(L, "reflow.new: prefix must be 1..15 bytes");
    }

    ensure_metatable(L);
    reflow_state *s = (reflow_state *)lua_newuserdata(L, sizeof(*s));
    memcpy(s->prefix, prefix, plen);
    s->prefix[plen] = '\0';
    s->prefix_len = plen;
    s->max_include_depth = max_include_depth;
    s->templates_ref = LUA_NOREF;
    s->helpers_ref = LUA_NOREF;
    s->helper_names_ref = LUA_NOREF;

    /* templates table */
    lua_newtable(L);
    s->templates_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    /* helpers table */
    lua_newtable(L);
    s->helpers_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    /* helper names set */
    lua_newtable(L);
    s->helper_names_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, REFLOW_STATE_MT);
    lua_setmetatable(L, -2);
    return 1;
}

int rf_state_add_helper(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    const char *name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    /* helpers[name] = function */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->helpers_ref);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);

    /* helper_names[name] = true */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->helper_names_ref);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);

    lua_settop(L, 1);
    return 1; /* return state for chaining */
}

/* Build a C string array of the currently registered helper names into
 * the compile arena. Returns count written to *n. */
static const char **helper_names_array(lua_State *L, reflow_state *s,
                                       compile_arena *carena, size_t *n)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->helper_names_ref);
    size_t cnt = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        cnt++;
        lua_pop(L, 1);
    }
    if (cnt == 0) {
        lua_pop(L, 1);
        *n = 0;
        return NULL;
    }
    const char **arr = (const char **)compile_arena_alloc(
        carena, L, cnt * sizeof(const char *));
    size_t idx = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        size_t nl = 0;
        const char *nm = lua_tolstring(L, -2, &nl);
        char *dup = (char *)compile_arena_alloc(carena, L, nl + 1);
        memcpy(dup, nm, nl);
        dup[nl] = '\0';
        arr[idx++] = dup;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    *n = idx;
    return arr;
}

int rf_state_compile(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    const char *name = luaL_checkstring(L, 2);
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 3, &hlen);

    compile_arena *carena = compile_arena_new(L, 8192);
    int carena_stack_pos = lua_gettop(L);

    /* Snapshot the current helper names for static validation. */
    size_t n_helpers = 0;
    const char **helpers = helper_names_array(L, s, carena, &n_helpers);

    reflow_error err = {0};
    ir_node *root = compile_template(carena, L,
                                     html, hlen,
                                     s->prefix, s->prefix_len,
                                     helpers, n_helpers, &err);
    if (root == NULL) {
        return luaL_error(L, "reflow:compile(\"%s\"): %s",
                          name, err.message ? err.message : "compile error");
    }

    /* Copy html source into arena for render-time snippets. */
    char *html_copy = (char *)compile_arena_alloc(carena, L, hlen + 1);
    memcpy(html_copy, html, hlen);
    html_copy[hlen] = '\0';

    /* templates[name] = { arena = carena_ud, root = lightud, html = string } */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    lua_createtable(L, 0, 3);
    lua_pushvalue(L, carena_stack_pos);
    lua_setfield(L, -2, "arena");
    lua_pushlightuserdata(L, root);
    lua_setfield(L, -2, "root");
    lua_pushlstring(L, html_copy, hlen);
    lua_setfield(L, -2, "html");
    lua_setfield(L, -2, name);
    lua_pop(L, 1); /* templates */

    lua_settop(L, 1);
    return 1;
}

/* Convert a Lua value (string / table) at index `idx` into a
 * reflow_value tree in the given render arena. String is JSON5, table
 * is deep-converted. Returns NULL + sets err on failure. */
static reflow_value *value_from_lua(lua_State *L, int idx,
                                    arena_t *arena,
                                    reflow_error *err)
{
    int t = lua_type(L, idx);
    if (t == LUA_TNIL || t == LUA_TNONE) {
        return NULL;
    }
    if (t == LUA_TSTRING) {
        size_t slen = 0;
        const char *s = lua_tolstring(L, idx, &slen);
        return json5_parse(s, slen, arena, err);
    }
    if (t == LUA_TTABLE) {
        /* Round-trip through JSON isn't ideal, but the simplest correct
         * conversion given we already have json5_parse. Serialize the
         * Lua table into a JSON5 string with lua_next → then parse. For
         * now support only the string form; tables can be added later. */
        if (err) {
            err->type = "ReflowRuntimeError";
            err->message =
                "render data: pass a JSON5 string (table form not yet "
                "supported)";
        }
        return NULL;
    }
    if (err) {
        err->type = "ReflowRuntimeError";
        err->message = "render data must be nil, a string, or a table";
    }
    return NULL;
}

int rf_state_render(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    const char *name = luaL_checkstring(L, 2);

    /* Look up template. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return luaL_error(L, "reflow:render(\"%s\"): template not registered",
                          name);
    }
    lua_getfield(L, -1, "root");
    ir_node *root = (ir_node *)lua_touserdata(L, -1);
    lua_pop(L, 3); /* root, entry, templates */

    /* Set up render arena. */
    arena_t rarena;
    arena_init(&rarena, NULL, 0);

    reflow_error derr = {0};
    reflow_value *globals = value_from_lua(L, 3, &rarena, &derr);
    if (derr.message != NULL) {
        arena_destroy(&rarena);
        return luaL_error(L, "reflow:render(\"%s\"): %s", name, derr.message);
    }

    /* Render. */
    buf_t out;
    if (buf_init(&out) != 0) {
        arena_destroy(&rarena);
        return luaL_error(L, "out of memory");
    }
    /* Push helpers table ref so eval sees it. */
    reflow_error rerr = {0};
    int rc = interpret_render(&rarena, root, globals, L,
                              s->helpers_ref, &out, &rerr);
    if (rc != 0) {
        buf_free(&out);
        arena_destroy(&rarena);
        return luaL_error(L, "reflow:render(\"%s\"): %s", name,
                          rerr.message ? rerr.message : "render error");
    }
    lua_pushlstring(L, out.data, out.len);
    buf_free(&out);
    arena_destroy(&rarena);
    return 1;
}

int rf_state_clear(lua_State *L)
{
    reflow_state *s = check_state(L, 1);

    lua_newtable(L);            /* result array */
    int result = lua_gettop(L);
    int n = 0;

    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    int tmpl = lua_gettop(L);

    if (lua_isnoneornil(L, 2)) {
        /* Collect every current name, then drop the whole table. */
        lua_pushnil(L);
        while (lua_next(L, tmpl)) {
            lua_pop(L, 1);              /* value */
            lua_pushvalue(L, -1);       /* dup key */
            lua_rawseti(L, result, ++n);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, s->templates_ref);
        lua_newtable(L);
        s->templates_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        const char *name = luaL_checkstring(L, 2);
        lua_getfield(L, tmpl, name);
        if (!lua_isnil(L, -1)) {
            lua_pushstring(L, name);
            lua_rawseti(L, result, ++n);
            lua_pushnil(L);
            lua_setfield(L, tmpl, name);
        }
        lua_pop(L, 1);
    }

    lua_settop(L, result);
    return 1;
}

int rf_state_templates(lua_State *L)
{
    reflow_state *s = check_state(L, 1);

    lua_newtable(L);
    int result = lua_gettop(L);
    int n = 0;

    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    int tmpl = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, tmpl)) {
        lua_pop(L, 1);              /* value */
        lua_pushvalue(L, -1);       /* dup key */
        lua_rawseti(L, result, ++n);
    }

    lua_settop(L, result);
    return 1;
}
