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
        {NULL,         NULL           },
    };

    lua_errno_loadlib(L);

    lua_newtable(L);
    for (struct luaL_Reg *ptr = method; ptr->name != NULL; ptr++) {
        lauxh_pushfn2tbl(L, ptr->name, ptr->func);
    }

    return 1;
}
