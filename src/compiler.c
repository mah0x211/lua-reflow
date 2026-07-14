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
#include "compile.h"
#include "compile_arena.h"
#include "directives.h"
#include "escape.h"
#include "expr/eval.h"
#include "expr/parse.h"
#include "interpret.h"
#include "json5.h"
#include "parser.h"
#include "renderer.h"
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

/* ============================================================ */
/* Parser test hook — dumps SAX events as a Lua array           */
/* ============================================================ */

typedef struct {
    lua_State *L;
    int        table_idx; /* absolute index of the events table */
    int        n;
} parse_ctx;

static void ev_push_string(lua_State *L, const char *s, size_t n)
{
    if (s == NULL) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, s, n);
    }
}

static void ev_element(void *ud,
                       const char *tag_name, size_t tag_len,
                       const sax_attr *attrs, size_t n_attrs,
                       bool self_closing,
                       size_t src_start, size_t src_end,
                       int token)
{
    parse_ctx *ctx = (parse_ctx *)ud;
    lua_State *L = ctx->L;
    lua_createtable(L, 7, 0);
    lua_pushliteral(L, "element");
    lua_rawseti(L, -2, 1);
    ev_push_string(L, tag_name, tag_len);
    lua_rawseti(L, -2, 2);
    /* attrs */
    lua_createtable(L, (int)n_attrs, 0);
    for (size_t i = 0; i < n_attrs; i++) {
        lua_createtable(L, 4, 0);
        ev_push_string(L, attrs[i].name, attrs[i].name_len);
        lua_rawseti(L, -2, 1);
        ev_push_string(L, attrs[i].value, attrs[i].value_len);
        lua_rawseti(L, -2, 2);
        lua_pushinteger(L, (lua_Integer)attrs[i].source_start);
        lua_rawseti(L, -2, 3);
        lua_pushinteger(L, (lua_Integer)attrs[i].source_end);
        lua_rawseti(L, -2, 4);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    lua_rawseti(L, -2, 3);
    lua_pushboolean(L, self_closing);
    lua_rawseti(L, -2, 4);
    lua_pushinteger(L, (lua_Integer)token);
    lua_rawseti(L, -2, 5);
    lua_pushinteger(L, (lua_Integer)src_start);
    lua_rawseti(L, -2, 6);
    lua_pushinteger(L, (lua_Integer)src_end);
    lua_rawseti(L, -2, 7);

    ctx->n++;
    lua_rawseti(L, ctx->table_idx, ctx->n);
}

static void ev_endtag(void *ud,
                      const char *tag_name, size_t tag_len,
                      int token)
{
    parse_ctx *ctx = (parse_ctx *)ud;
    lua_State *L = ctx->L;
    lua_createtable(L, 3, 0);
    lua_pushliteral(L, "endtag");
    lua_rawseti(L, -2, 1);
    ev_push_string(L, tag_name, tag_len);
    lua_rawseti(L, -2, 2);
    lua_pushinteger(L, (lua_Integer)token);
    lua_rawseti(L, -2, 3);
    ctx->n++;
    lua_rawseti(L, ctx->table_idx, ctx->n);
}

static void ev_text(void *ud, const char *text, size_t len,
                    bool last_in_text_node)
{
    parse_ctx *ctx = (parse_ctx *)ud;
    lua_State *L = ctx->L;
    lua_createtable(L, 3, 0);
    lua_pushliteral(L, "text");
    lua_rawseti(L, -2, 1);
    ev_push_string(L, text, len);
    lua_rawseti(L, -2, 2);
    lua_pushboolean(L, last_in_text_node);
    lua_rawseti(L, -2, 3);
    ctx->n++;
    lua_rawseti(L, ctx->table_idx, ctx->n);
}

static void ev_comment(void *ud, const char *text, size_t len)
{
    parse_ctx *ctx = (parse_ctx *)ud;
    lua_State *L = ctx->L;
    lua_createtable(L, 2, 0);
    lua_pushliteral(L, "comment");
    lua_rawseti(L, -2, 1);
    ev_push_string(L, text, len);
    lua_rawseti(L, -2, 2);
    ctx->n++;
    lua_rawseti(L, ctx->table_idx, ctx->n);
}

/* parse_html: test hook — tokenize HTML and return an events array.
 * Each event is a table starting with a type tag:
 *   {"element", tag, {attrs}, self_closing, token, src_start, src_end}
 *   {"endtag", tag, token}
 *   {"text", data, last_in_text_node}
 *   {"comment", data}
 * Attribute entries are {name, value, source_start, source_end}
 * (value is nil when the attribute has no value). */
static int parse_html_lua(lua_State *L)
{
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);

    lua_newtable(L);
    parse_ctx ctx = {
        .L = L,
        .table_idx = lua_gettop(L),
        .n = 0,
    };
    sax_handler handler = {
        .ud         = &ctx,
        .on_element = ev_element,
        .on_endtag  = ev_endtag,
        .on_text    = ev_text,
        .on_comment = ev_comment,
    };
    reflow_error err = {0};
    int rc = html_parse(html, hlen, &handler, &err);
    if (rc != 0 || err.message != NULL) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "parse error");
        return 2;
    }
    return 1;
}

/* ============================================================ */
/* compile_template test hook — dumps the IR tree as a table    */
/* ============================================================ */

static void ir_to_lua(lua_State *L, const ir_node *node);

static void ir_children_to_lua(lua_State *L,
                               struct ir_node * const *children,
                               size_t n_children)
{
    lua_createtable(L, (int)n_children, 0);
    for (size_t i = 0; i < n_children; i++) {
        ir_to_lua(L, children[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
}

static void ir_to_lua(lua_State *L, const ir_node *node)
{
    switch (node->type) {
    case IR_ROOT:
        lua_createtable(L, 0, 2);
        lua_pushliteral(L, "root");
        lua_setfield(L, -2, "type");
        ir_children_to_lua(L, node->root.children, node->root.n_children);
        lua_setfield(L, -2, "children");
        return;

    case IR_ELEMENT:
        lua_createtable(L, 0, 7);
        lua_pushliteral(L, "element");
        lua_setfield(L, -2, "type");
        lua_pushstring(L, node->element.tag_name);
        lua_setfield(L, -2, "tag");
        lua_createtable(L, (int)node->element.n_attrs, 0);
        for (size_t i = 0; i < node->element.n_attrs; i++) {
            lua_createtable(L, 2, 0);
            lua_pushstring(L, node->element.attrs[i].name);
            lua_rawseti(L, -2, 1);
            if (node->element.attrs[i].value != NULL) {
                lua_pushstring(L, node->element.attrs[i].value);
            } else {
                lua_pushnil(L);
            }
            lua_rawseti(L, -2, 2);
            lua_rawseti(L, -2, (int)(i + 1));
        }
        lua_setfield(L, -2, "attrs");
        /* directives — summary form for testing */
        {
            const ir_directives *d = &node->element.directives;
            lua_createtable(L, 0, 16);
            if (d->data_raw) {
                lua_pushlstring(L, d->data_raw, d->data_raw_len);
                lua_setfield(L, -2, "data_raw");
            }
            if (d->with_bindings) {
                lua_createtable(L, (int)d->n_with, 0);
                for (size_t i = 0; i < d->n_with; i++) {
                    lua_pushstring(L, d->with_bindings[i].name);
                    lua_rawseti(L, -2, (int)(i + 1));
                }
                lua_setfield(L, -2, "with");
            }
            if (d->text_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "text");
            }
            if (d->html_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "html");
            }
            if (d->include_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "include");
            }
            if (d->for_spec) {
                lua_createtable(L, 0, 4);
                lua_pushstring(L, d->for_spec->var_name);
                lua_setfield(L, -2, "var");
                lua_pushnumber(L, d->for_spec->start);
                lua_setfield(L, -2, "start");
                lua_pushnumber(L, d->for_spec->stop);
                lua_setfield(L, -2, "stop");
                lua_pushnumber(L, d->for_spec->step);
                lua_setfield(L, -2, "step");
                lua_setfield(L, -2, "for_spec");
            }
            if (d->each_spec) {
                lua_createtable(L, 0, 2);
                lua_pushstring(L, d->each_spec->item_name);
                lua_setfield(L, -2, "item");
                if (d->each_spec->index_name) {
                    lua_pushstring(L, d->each_spec->index_name);
                    lua_setfield(L, -2, "index");
                }
                lua_setfield(L, -2, "each_spec");
            }
            if (d->if_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "if_expr");
            }
            if (d->elseif_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "elseif_expr");
            }
            if (d->else_mark) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "else_mark");
            }
            if (d->match_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "match_expr");
            }
            if (d->case_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "case_expr");
            }
            if (d->nocase_mark) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "nocase_mark");
            }
            if (d->break_mark) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "break_mark");
            }
            if (d->break_if_expr) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, "break_if_expr");
            }
            if (d->n_binds > 0) {
                lua_createtable(L, (int)d->n_binds, 0);
                for (size_t i = 0; i < d->n_binds; i++) {
                    lua_pushstring(L, d->binds[i].attr_name);
                    lua_rawseti(L, -2, (int)(i + 1));
                }
                lua_setfield(L, -2, "binds");
            }
            lua_setfield(L, -2, "directives");
        }
        if (node->element.invisible_marker) {
            lua_pushboolean(L, 1);
            lua_setfield(L, -2, "invisible_marker");
        }
        ir_children_to_lua(L, node->element.children,
                           node->element.n_children);
        lua_setfield(L, -2, "children");
        lua_pushinteger(L, (lua_Integer)node->element.source_start);
        lua_setfield(L, -2, "source_start");
        lua_pushinteger(L, (lua_Integer)node->element.source_end);
        lua_setfield(L, -2, "source_end");
        return;

    case IR_TEXT:
        lua_createtable(L, 0, 2);
        lua_pushliteral(L, "text");
        lua_setfield(L, -2, "type");
        lua_pushlstring(L, node->text.text, node->text.text_len);
        lua_setfield(L, -2, "text");
        return;

    case IR_COMMENT:
        lua_createtable(L, 0, 2);
        lua_pushliteral(L, "comment");
        lua_setfield(L, -2, "type");
        lua_pushlstring(L, node->comment.text, node->comment.text_len);
        lua_setfield(L, -2, "text");
        return;

    case IR_CHAIN:
        /* Chains are produced by a later stage; represent minimally. */
        lua_createtable(L, 0, 2);
        lua_pushliteral(L, "chain");
        lua_setfield(L, -2, "type");
        lua_pushinteger(L, (lua_Integer)node->chain.n_branches);
        lua_setfield(L, -2, "n_branches");
        return;
    }
}

/* compile_template: test hook — compile HTML and return the IR tree
 * as nested Lua tables.
 * args: html [, opts]  where opts = { prefix?, helpers? }
 *   prefix defaults to "x-"
 *   helpers may be an array of names or a set (t[name] = true)
 * Returns nil + error message on failure. */
static int compile_template_lua(lua_State *L)
{
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);

    const char *prefix = "x-";
    size_t prefix_len = 2;
    const char **helpers = NULL;
    size_t n_helpers = 0;

    compile_arena *carena = compile_arena_new(L, 4096);
    int carena_stack_pos = lua_gettop(L);

    if (lua_istable(L, 2)) {
        /* prefix */
        lua_getfield(L, 2, "prefix");
        if (lua_isstring(L, -1)) {
            prefix = lua_tolstring(L, -1, &prefix_len);
        }
        lua_pop(L, 1);
        /* helpers: array or set */
        lua_getfield(L, 2, "helpers");
        if (lua_istable(L, -1)) {
            /* Count entries: first try as array (ipairs), else pairs. */
            size_t alen = 0;
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
            alen = (size_t)lua_rawlen(L, -1);
#else
            alen = (size_t)lua_objlen(L, -1);
#endif
            if (alen > 0) {
                helpers = (const char **)compile_arena_alloc(
                    carena, L, alen * sizeof(const char *));
                for (size_t i = 0; i < alen; i++) {
                    lua_rawgeti(L, -1, (int)(i + 1));
                    size_t nl = 0;
                    const char *nm = luaL_checklstring(L, -1, &nl);
                    char *dup = (char *)compile_arena_alloc(
                        carena, L, nl + 1);
                    memcpy(dup, nm, nl);
                    dup[nl] = '\0';
                    helpers[i] = dup;
                    lua_pop(L, 1);
                }
                n_helpers = alen;
            } else {
                /* set form: t[name] = true */
                lua_pushnil(L);
                size_t cnt = 0;
                while (lua_next(L, -2)) {
                    cnt++;
                    lua_pop(L, 1);
                }
                helpers = (const char **)compile_arena_alloc(
                    carena, L, cnt * sizeof(const char *));
                lua_pushnil(L);
                size_t idx = 0;
                while (lua_next(L, -2)) {
                    size_t nl = 0;
                    const char *nm = lua_tolstring(L, -2, &nl);
                    char *dup = (char *)compile_arena_alloc(
                        carena, L, nl + 1);
                    memcpy(dup, nm, nl);
                    dup[nl] = '\0';
                    helpers[idx++] = dup;
                    lua_pop(L, 1);
                }
                n_helpers = idx;
            }
        }
        lua_pop(L, 1);
    }

    reflow_error err = {0};
    ir_node *root = compile_template(carena, L,
                                     NULL /* template_name */,
                                     html, hlen,
                                     prefix, prefix_len,
                                     helpers, n_helpers,
                                     &err);
    if (root == NULL) {
        lua_settop(L, carena_stack_pos);
        lua_pop(L, 1); /* remove carena */
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "compile error");
        return 2;
    }
    ir_to_lua(L, root);
    /* Result table now on top; drop the carena userdata beneath it. */
    lua_remove(L, carena_stack_pos);
    return 1;
}

/* render: test hook — compile HTML and render against a JSON5 data string.
 * args: html, data_json5?, opts?
 *   opts = { prefix?, helpers? = { name = lua_function, ... } }
 * Returns the rendered HTML string, or nil + error message. */
static int render_lua(lua_State *L)
{
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);
    const char *data = NULL;
    size_t data_len = 0;
    if (lua_type(L, 2) == LUA_TSTRING) {
        data = lua_tolstring(L, 2, &data_len);
    }

    const char *prefix = "x-";
    size_t prefix_len = 2;
    const char **helper_names = NULL;
    size_t n_helpers = 0;
    int helpers_ref = LUA_NOREF;

    compile_arena *carena = compile_arena_new(L, 4096);
    int carena_stack_pos = lua_gettop(L);

    /* Parse options for prefix + helpers. */
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "prefix");
        if (lua_isstring(L, -1)) {
            prefix = lua_tolstring(L, -1, &prefix_len);
        }
        lua_pop(L, 1);

        lua_getfield(L, 3, "helpers");
        if (lua_istable(L, -1)) {
            /* Build helper name array from table keys. */
            size_t cnt = 0;
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                cnt++;
                lua_pop(L, 1);
            }
            if (cnt > 0) {
                helper_names = (const char **)compile_arena_alloc(
                    carena, L, cnt * sizeof(const char *));
                size_t idx = 0;
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    size_t nl = 0;
                    const char *nm = lua_tolstring(L, -2, &nl);
                    char *dup = (char *)compile_arena_alloc(
                        carena, L, nl + 1);
                    memcpy(dup, nm, nl);
                    dup[nl] = '\0';
                    helper_names[idx++] = dup;
                    lua_pop(L, 1);
                }
                n_helpers = idx;
            }
            /* Store the helpers table itself in the registry for
             * name→function lookup at eval time. */
            lua_pushvalue(L, -1);
            helpers_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        lua_pop(L, 1);
    }

    /* 1. Compile. */
    reflow_error err = {0};
    ir_node *root = compile_template(carena, L,
                                     NULL /* template_name */,
                                     html, hlen,
                                     prefix, prefix_len,
                                     helper_names, n_helpers, &err);
    if (root == NULL) {
        if (helpers_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        lua_settop(L, carena_stack_pos);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, err.message ? err.message : "compile error");
        return 2;
    }

    /* 2. Set up render arena and parse render data (JSON5). */
    arena_t rarena;
    arena_init(&rarena, NULL, 0);

    reflow_value *globals = NULL;
    if (data != NULL) {
        reflow_error derr = {0};
        globals = json5_parse(data, data_len, &rarena, &derr);
        if (globals == NULL) {
            arena_destroy(&rarena);
            if (helpers_ref != LUA_NOREF)
                luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
            lua_settop(L, carena_stack_pos);
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushstring(L, derr.message ? derr.message : "data error");
            return 2;
        }
    }

    /* 3. Render into a buf_t. */
    buf_t out;
    if (buf_init(&out) != 0) {
        arena_destroy(&rarena);
        if (helpers_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        return luaL_error(L, "out of memory");
    }
    reflow_error rerr = {0};
    int rc = interpret_render(&rarena, root, globals, L, helpers_ref,
                              NULL, html, hlen,
                              NULL, &out, &rerr);
    if (rc != 0) {
        buf_free(&out);
        arena_destroy(&rarena);
        if (helpers_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        lua_settop(L, carena_stack_pos);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, rerr.message ? rerr.message : "render error");
        return 2;
    }
    lua_pushlstring(L, out.data, out.len);
    buf_free(&out);
    arena_destroy(&rarena);
    if (helpers_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
    lua_remove(L, carena_stack_pos);
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
        {"parse_html",       parse_html_lua      },
        {"compile_template", compile_template_lua},
        {"render",           render_lua          },
        /* State-based public API (used by lua/reflow.lua). */
        {"state_new",        rf_state_new        },
        {"state_add_helper", rf_state_add_helper },
        {"state_compile",    rf_state_compile    },
        {"state_render",     rf_state_render     },
        {"state_clear",      rf_state_clear      },
        {"state_templates",  rf_state_templates  },
        {NULL,         NULL           },
    };

    lua_errno_loadlib(L);
    /* Install the reflow.error metatable so structured errors raised
     * from renderer.c produce a meaningful tostring(). */
    reflow_register_error_metatable(L);

    lua_newtable(L);
    for (struct luaL_Reg *ptr = method; ptr->name != NULL; ptr++) {
        lauxh_pushfn2tbl(L, ptr->name, ptr->func);
    }

    return 1;
}
