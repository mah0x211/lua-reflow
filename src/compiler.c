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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

// project
#include "arena.h"
#include "compile_arena.h"
#include "directives.h"
#include "escape.h"
#include "expr/eval.h"
#include "expr/parse.h"
#include "json5.h"
#include "scope.h"
#include "value.h"
// depend
#include "lauxhlib.h"
#include "lua_errno.h"
#include "yyjson.h"
// lua
#include <lauxlib.h>
// system
#include <stddef.h>

/*
 * reflow.compiler - HTML -> IR module.
 *
 * Currently exposes version() and a yyjson smoke test that verifies the
 * yyjson amalgamation compiles, links, and runs.
 */

static int version_lua(lua_State *L)
{
    lua_pushliteral(L, "0.0.0-dev");
    return 1;
}

/* Smoke test: the yyjson amalgamation compiles, links, and runs. */
static int yyjson_ok_lua(lua_State *L)
{
    yyjson_doc *doc = yyjson_read("{}", 2, YYJSON_READ_NOFLAG);
    int ok          = (doc != NULL);

    if (doc != NULL) {
        yyjson_doc_free(doc);
    }
    lua_pushboolean(L, ok);
    return 1;
}

/* number_to_string: JS String(n)-compatible formatting (verified). */
static int ntos_lua(lua_State *L)
{
    double n   = luaL_checknumber(L, 1);
    char buf[64];
    size_t len = number_to_string(n, buf);
    lua_pushlstring(L, buf, len);
    return 1;
}

/* escape_text: OWASP 5 chars. */
static int esct_lua(lua_State *L)
{
    size_t slen    = 0;
    const char *s  = luaL_checklstring(L, 1, &slen);
    buf_t out;
    if (buf_init(&out) != 0) {
        return luaL_error(L, "out of memory");
    }
    escape_text(s, slen, &out);
    lua_pushlstring(L, out.data, out.len);
    buf_free(&out);
    return 1;
}

/* escape_attr: 4 chars, double-quoted context. */
static int esca_lua(lua_State *L)
{
    size_t slen    = 0;
    const char *s  = luaL_checklstring(L, 1, &slen);
    buf_t out;
    if (buf_init(&out) != 0) {
        return luaL_error(L, "out of memory");
    }
    escape_attr(s, slen, &out);
    lua_pushlstring(L, out.data, out.len);
    buf_free(&out);
    return 1;
}

/* parse_expr: test hook — parse an expression string.
 * Returns "ok" on success, or nil + error message on failure. */
static int parse_expr_lua(lua_State *L)
{
    size_t slen    = 0;
    const char *src = luaL_checklstring(L, 1, &slen);

    /* Create arena (userdata pushed onto stack) */
    compile_arena *arena = compile_arena_new(L, 4096);

    reflow_error err;
    memset(&err, 0, sizeof(err));

    expr_node *node = expr_parse(arena, L, src, slen, &err);
    /* Pop arena regardless (AST lives in arena; test hook discards it) */
    lua_pop(L, 1);

    if (!node) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }
    lua_pushliteral(L, "ok");
    return 1;
}

/* eval_expr: test hook — parse + evaluate an expression against JSON data.
 * args: expr_str, data_json, helpers_tbl?
 * returns: result value, or nil + error message */
static int eval_expr_lua(lua_State *L)
{
    size_t expr_len = 0, data_len = 0;
    const char *expr_str = luaL_checklstring(L, 1, &expr_len);
    const char *data_str = luaL_checklstring(L, 2, &data_len);

    /* 1. Parse expression (compile_arena; may throw OOM) */
    compile_arena *carena = compile_arena_new(L, 4096);
    reflow_error  err = {0};
    expr_node    *node = expr_parse(carena, L, expr_str, expr_len, &err);
    lua_pop(L, 1);  /* remove carena */

    if (!node) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }

    /* 2. Parse JSON data (render-scope arena) */
    char     arena_buf[65536];
    arena_t  rarena;
    arena_init(&rarena, arena_buf, sizeof(arena_buf));

    reflow_value *globals = json5_parse(data_str, data_len, &rarena, &err);
    if (!globals) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "json error");
        return 2;
    }

    /* 3. Set up helpers (after OOM-prone code) */
    int helpers_ref = LUA_NOREF;
    if (lua_istable(L, 3)) {
        lua_pushvalue(L, 3);
        helpers_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    /* 4. Evaluate */
    scope_env env;
    scope_env_init(&env, globals);
    scope_push_frame(&env, SCOPE_FRAME_DATA, globals);
    reflow_value result = expr_eval(node, &env, L, helpers_ref,
                                    &rarena, &err);

    /* 5. Clean up helpers ref */
    if (helpers_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);

    if (err.message) {
        lua_pushnil(L);
        lua_pushstring(L, err.message);
        return 2;
    }

    /* 6. Return result as Lua value */
    rv_to_lua(L, &result);
    return 1;
}

/* directives_parse_data: test hook — parse an x-data value.
 * returns: table of scopes, or nil + error message */
static int dir_parse_data_lua(lua_State *L)
{
    size_t vlen = 0;
    const char *value = luaL_checklstring(L, 1, &vlen);

    compile_arena *carena = compile_arena_new(L, 4096);
    char     abuf[65536];
    arena_t  rarena;
    arena_init(&rarena, abuf, sizeof(abuf));
    reflow_error err = {0};

    reflow_value *rv = directives_parse_data(carena, L, &rarena,
                                             value, vlen, &err);
    lua_pop(L, 1); /* remove carena */
    if (!rv) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }
    rv_to_lua(L, rv);
    return 1;
}

/* directives_parse_with: test hook.
 * returns: array of binding-name strings, or nil + error message */
static int dir_parse_with_lua(lua_State *L)
{
    size_t vlen = 0;
    const char *value = luaL_checklstring(L, 1, &vlen);

    compile_arena *carena = compile_arena_new(L, 4096);
    reflow_error err = {0};
    ir_with_result r = directives_parse_with(carena, L, value, vlen, &err);
    lua_pop(L, 1);

    if (err.message) {
        lua_pushnil(L);
        lua_pushstring(L, err.message);
        return 2;
    }
    lua_createtable(L, (int)r.n, 0);
    for (size_t i = 0; i < r.n; i++) {
        lua_pushstring(L, r.bindings[i].name);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

/* directives_parse_for: test hook.
 * returns: table {var, start, stop, step}, or nil + error message */
static int dir_parse_for_lua(lua_State *L)
{
    size_t vlen = 0;
    const char *value = luaL_checklstring(L, 1, &vlen);

    compile_arena *carena = compile_arena_new(L, 4096);
    reflow_error err = {0};
    ir_for_spec *spec = directives_parse_for(carena, L, value, vlen, &err);
    lua_pop(L, 1);

    if (!spec) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }
    lua_createtable(L, 0, 4);
    lua_pushstring(L, spec->var_name);
    lua_setfield(L, -2, "var");
    lua_pushnumber(L, spec->start);
    lua_setfield(L, -2, "start");
    lua_pushnumber(L, spec->stop);
    lua_setfield(L, -2, "stop");
    lua_pushnumber(L, spec->step);
    lua_setfield(L, -2, "step");
    return 1;
}

/* directives_parse_each: test hook.
 * returns: table {item, index, has_collection}, or nil + error message */
static int dir_parse_each_lua(lua_State *L)
{
    size_t vlen = 0;
    const char *value = luaL_checklstring(L, 1, &vlen);

    compile_arena *carena = compile_arena_new(L, 4096);
    reflow_error err = {0};
    ir_each_spec *spec = directives_parse_each(carena, L, value, vlen, &err);
    lua_pop(L, 1);

    if (!spec) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }
    lua_createtable(L, 0, 3);
    lua_pushstring(L, spec->item_name);
    lua_setfield(L, -2, "item");
    if (spec->index_name) {
        lua_pushstring(L, spec->index_name);
        lua_setfield(L, -2, "index");
    }
    lua_pushboolean(L, spec->collection != NULL);
    lua_setfield(L, -2, "has_collection");
    return 1;
}

/* directives_parse_expr: test hook — parse an expression-valued directive.
 * args: value, directive_name.  returns: "ok" or nil + error. */
static int dir_parse_expr_lua(lua_State *L)
{
    size_t vlen = 0, nlen = 0;
    const char *value = luaL_checklstring(L, 1, &vlen);
    const char *name  = luaL_checklstring(L, 2, &nlen);
    (void)nlen;

    compile_arena *carena = compile_arena_new(L, 4096);
    reflow_error err = {0};
    expr_node *node = directives_parse_expr(carena, L, value, vlen,
                                            name, &err);
    lua_pop(L, 1);

    if (!node) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }
    lua_pushliteral(L, "ok");
    return 1;
}

/* directives_assert_empty: test hook.
 * args: value (nil or string), directive_name.  returns: "ok" or nil + err. */
static int dir_assert_empty_lua(lua_State *L)
{
    const char *value = NULL;
    size_t vlen = 0;
    if (!lua_isnoneornil(L, 1)) {
        value = luaL_checklstring(L, 1, &vlen);
    }
    const char *name = luaL_checkstring(L, 2);

    reflow_error err = {0};
    if (directives_assert_empty(value, vlen, name, &err) != 0) {
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "value not empty");
        return 2;
    }
    lua_pushliteral(L, "ok");
    return 1;
}

/* directives_is_known: test hook. */
static int dir_is_known_lua(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    lua_pushboolean(L, directives_is_known(name));
    return 1;
}

/* directives_group: test hook.  Returns the single-letter group char, or nil. */
static int dir_group_lua(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char g = directives_group(name);
    if (g == 0) {
        lua_pushnil(L);
    } else {
        char buf[2] = { g, '\0' };
        lua_pushstring(L, buf);
    }
    return 1;
}

LUALIB_API int luaopen_reflow_compiler(lua_State *L)
{
    struct luaL_Reg method[] = {
        {"version",   version_lua  },
        {"yyjson_ok", yyjson_ok_lua},
        {"ntos",      ntos_lua     },
        {"esct",      esct_lua     },
        {"esca",      esca_lua     },
        {"parse_expr", parse_expr_lua },
        {"eval_expr",  eval_expr_lua  },
        {"dir_parse_data",   dir_parse_data_lua  },
        {"dir_parse_with",   dir_parse_with_lua  },
        {"dir_parse_for",    dir_parse_for_lua   },
        {"dir_parse_each",   dir_parse_each_lua  },
        {"dir_parse_expr",   dir_parse_expr_lua  },
        {"dir_assert_empty", dir_assert_empty_lua},
        {"dir_is_known",     dir_is_known_lua    },
        {"dir_group",        dir_group_lua       },
        {NULL,         NULL           },
    };

    lua_errno_loadlib(L);

    lua_newtable(L);
    for (struct luaL_Reg *ptr = method; ptr->name != NULL; ptr++) {
        lauxh_pushfn2tbl(L, ptr->name, ptr->func);
    }

    return 1;
}
