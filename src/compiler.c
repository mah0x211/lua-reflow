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
#include "escape.h"
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

LUALIB_API int luaopen_reflow_compiler(lua_State *L)
{
    struct luaL_Reg method[] = {
        {"version",   version_lua  },
        {"yyjson_ok", yyjson_ok_lua},
        {"ntos",      ntos_lua     },
        {"esct",      esct_lua     },
        {"esca",      esca_lua     },
        {NULL,        NULL         },
    };

    lua_errno_loadlib(L);

    lua_newtable(L);
    for (struct luaL_Reg *ptr = method; ptr->name != NULL; ptr++) {
        lauxh_pushfn2tbl(L, ptr->name, ptr->func);
    }

    return 1;
}
