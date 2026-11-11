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
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "compat.h"
#include "error.h"
#include "ir.h"
#include "pool.h"
#include "reflow_util.h"
#include "selector.h"
#include "selector_compile.h"
#include "selector_exec.h"

#define SELECTOR_OPERATION   "reflow.selector.compile"
#define SELECTOR_OOM_MESSAGE "out of memory while compiling the selector"
#define RESOLVE_OPERATION    "reflow.selector.resolve"
#define RESOLVE_OOM_MESSAGE  "out of memory while resolving the selector"

/* The userdata of a compiled selector. Its uservalue keeps the pool that
 * holds the source and every condition. */
typedef struct {
    selector_t public;
    pool_t *pool;
} selector_owner_t;

enum {
    SELECTOR_USERVALUE_POOL = 1
};

typedef struct {
    const char *source;
    size_t len;
    selector_parse_error_t err;
    int oom;
} compile_ctx_t;

static int selector_gc_lua(lua_State *L)
{
    selector_owner_t *owner = (selector_owner_t *)lua_touserdata(L, 1);

    if (owner != NULL && owner->pool != NULL) {
        pool_delete(L, owner->pool);
        owner->pool   = NULL;
        owner->public = (selector_t){0};
    }
    return 0;
}

/* Protected body: create the selector userdata and parse the source into
 * its pool. Leaves the userdata on success; false, the message, and the
 * feature on a parse error; or nothing when the pool runs out of memory. */
static int compile_protected(lua_State *L)
{
    compile_ctx_t *ctx      = (compile_ctx_t *)lua_touserdata(L, 1);
    selector_owner_t *owner = NULL;
    pool_t *pool            = NULL;
    char *source            = NULL;
    int owner_idx           = 0;
    int uservalue_idx       = 0;
    int rc                  = 0;

    lua_settop(L, 0);
    owner     = (selector_owner_t *)lua_newuserdata(L, sizeof(*owner));
    *owner    = (selector_owner_t){0};
    owner_idx = lua_gettop(L);
    luaL_getmetatable(L, REFLOW_SELECTOR_MT);
    lua_setmetatable(L, owner_idx);
    lua_createtable(L, 1, 0);
    uservalue_idx = lua_gettop(L);
    lua_pushvalue(L, uservalue_idx);
    reflow_setuservalue(L, owner_idx);

    if ((pool = pool_new(L, 0)) == NULL) {
        ctx->oom = 1;
        return 0;
    }
    /* Keep the pool reachable before the finalizer can see it. */
    lua_rawseti(L, uservalue_idx, SELECTOR_USERVALUE_POOL);
    owner->pool = pool;

    if ((source = (char *)pool_alloc(pool, ctx->len + 1)) == NULL) {
        ctx->oom = 1;
        return 0;
    }
    memcpy(source, ctx->source, ctx->len);
    source[ctx->len]     = '\0';
    owner->public.source = (selector_string_t){source, ctx->len};

    rc = selector_parse(L, pool, &owner->public, &ctx->err);
    if (rc < 0) {
        ctx->oom = 1;
        return 0;
    } else if (rc > 0) {
        /* The message and the feature are on top. */
        lua_pushboolean(L, 0);
        lua_insert(L, -3);
        return 3;
    }
    lua_settop(L, owner_idx);
    return 1;
}

/* Push reflow.error.new_selector(op, message, nil, meta) with the message at
 * message_idx, which may hold any byte, and the meta on top, which it pops. */
static void push_error(lua_State *L, const char *op, int message_idx)
{
    int meta_idx = lua_gettop(L);

    reflow_error_require(L, "reflow.error");
    lua_getfield(L, -1, "new_selector");
    lua_remove(L, -2);
    lua_pushstring(L, op);
    lua_pushvalue(L, message_idx);
    lua_pushnil(L);
    lua_pushvalue(L, meta_idx);
    lua_call(L, 4, 1);
    lua_remove(L, meta_idx);
}

/* Return nil and a selector error with the message at message_idx.
 * source_idx is the source as given, and
 * pos its 0-based byte offset; the meta also gives the offset in UTF-16 code
 * units, as JavaScript does, and 1-based byte and character positions.
 * feature_idx is 0 when no feature applies; reason is NULL for an
 * out-of-memory failure. */
static int return_selector_error(lua_State *L, int message_idx,
                                 const char *reason, int source_idx, size_t pos,
                                 int feature_idx)
{
    if (message_idx < 0) {
        message_idx = lua_gettop(L) + message_idx + 1;
    }
    lua_createtable(L, 0, 6);
    lua_pushvalue(L, source_idx);
    lua_setfield(L, -2, "source");
    if (reason != NULL) {
        const char *s = lua_type(L, source_idx) == LUA_TSTRING ?
                            lua_tostring(L, source_idx) :
                            NULL;
        size_t units  = 0;
        size_t chars  = 0;
        size_t i      = 0;

        for (i = 0; s != NULL && i < pos; i++) {
            unsigned char c = (unsigned char)s[i];

            if ((c & 0xc0) != 0x80) {
                chars++;
                units += c >= 0xf0 ? 2 : 1;
            }
        }
        lua_pushstring(L, reason);
        lua_setfield(L, -2, "reason");
        lua_pushinteger(L, (lua_Integer)units);
        lua_setfield(L, -2, "position");
        lua_pushinteger(L, (lua_Integer)pos + 1);
        lua_setfield(L, -2, "byte");
        lua_pushinteger(L, (lua_Integer)chars + 1);
        lua_setfield(L, -2, "char");
        if (feature_idx != 0) {
            lua_pushvalue(L, feature_idx);
            lua_setfield(L, -2, "feature");
        }
    }
    push_error(L, SELECTOR_OPERATION, message_idx);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

static int compile_lua(lua_State *L)
{
    compile_ctx_t ctx = {0};
    int base          = 0;
    int status        = 0;

    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_settop(L, 1);
        lua_pushliteral(
            L, "selector syntax error: selector source must be a string");
        return return_selector_error(L, -1, "syntax", 1, 0, 0);
    }
    ctx.source = lua_tolstring(L, 1, &ctx.len);
    if (reflow_js_is_blank(ctx.source, ctx.len)) {
        lua_settop(L, 1);
        lua_pushliteral(L, "selector syntax error: empty selector");
        return return_selector_error(L, -1, "syntax", 1, 0, 0);
    }
    lua_settop(L, 1);
    base = lua_gettop(L);
    lua_pushcfunction(L, compile_protected);
    lua_pushlightuserdata(L, &ctx);
    status = lua_pcall(L, 1, LUA_MULTRET, 0);
    if (status == LUA_OK && !ctx.oom && lua_gettop(L) == base + 1) {
        return 1;
    } else if (status == LUA_OK && !ctx.oom) {
        /* false, message, feature */
        return return_selector_error(
            L, base + 2, ctx.err.unsupported ? "unsupported" : "syntax", 1,
            ctx.err.pos, lua_isnil(L, base + 3) ? 0 : base + 3);
    } else if (status != LUA_OK && status != LUA_ERRMEM) {
        return lua_error(L);
    }
    lua_settop(L, base);
    /* Reclaim the failed selector before building the error. */
    lua_gc(L, LUA_GCCOLLECT, 0);
    lua_pushliteral(L, SELECTOR_OOM_MESSAGE);
    return return_selector_error(L, -1, NULL, 1, 0, 0);
}

static int is_selector(lua_State *L, int idx)
{
    int same = 0;

    if (lua_type(L, idx) != LUA_TUSERDATA || !lua_getmetatable(L, idx)) {
        return 0;
    }
    luaL_getmetatable(L, REFLOW_SELECTOR_MT);
    same = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return same;
}

static int is_compiled_lua(lua_State *L)
{
    lua_pushboolean(L, is_selector(L, 1));
    return 1;
}

static int source_lua(lua_State *L)
{
    const selector_t *sel = selector_check(L, 1);

    lua_pushlstring(L, sel->source.data, sel->source.len);
    return 1;
}

static void push_string(lua_State *L, const selector_string_t *s)
{
    lua_pushlstring(L, s->data, s->len);
}

static void set_string(lua_State *L, const char *key,
                       const selector_string_t *s)
{
    if (s->data != NULL) {
        push_string(L, s);
        lua_setfield(L, -2, key);
    }
}

static const char *const PSEUDO_NAMES[] = {
    "first-child",  "last-child",       "only-child", "first-of-type",
    "last-of-type", "only-of-type",     "nth-child",  "nth-last-child",
    "nth-of-type",  "nth-last-of-type",
};

/* Push the positional conditions of c as { {name = ..., n = ...}, ... }. */
static void push_pseudos(lua_State *L, const selector_compound_t *c)
{
    size_t i = 0;

    luaL_checkstack(L, 3, "building the selector table");
    lua_createtable(L, (int)c->npseudos, 0);
    for (i = 0; i < c->npseudos; i++) {
        const selector_pseudo_t *pseudo = &c->pseudos[i];

        lua_createtable(L, 0, 2);
        lua_pushstring(L, PSEUDO_NAMES[pseudo->kind]);
        lua_setfield(L, -2, "name");
        if (pseudo->kind >= SELECTOR_PSEUDO_NTH_CHILD) {
            /* A Lua integer when the argument is one, as JavaScript has a
             * single number type. */
            if (pseudo->n <= 9007199254740992.0) {
                lua_pushinteger(L, (lua_Integer)pseudo->n);
            } else {
                lua_pushnumber(L, (lua_Number)pseudo->n);
            }
            lua_setfield(L, -2, "n");
        }
        lua_rawseti(L, -2, (int)i + 1);
    }
}

static void push_compound(lua_State *L, const selector_compound_t *c)
{
    static const char *const ops[] = {NULL, "=", "~=", "|=", "^=", "$=", "*="};
    size_t i                       = 0;

    luaL_checkstack(L, 4, "building the selector table");
    lua_createtable(L, 0, 5);
    set_string(L, "tag", &c->tag);
    set_string(L, "id", &c->id);
    lua_createtable(L, (int)c->nclasses, 0);
    for (i = 0; i < c->nclasses; i++) {
        push_string(L, &c->classes[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "classes");
    lua_createtable(L, (int)c->nattrs, 0);
    for (i = 0; i < c->nattrs; i++) {
        const selector_attr_t *attr = &c->attrs[i];

        lua_createtable(L, 0, 3);
        set_string(L, "name", &attr->name);
        if (attr->op != SELECTOR_ATTR_EXISTS) {
            lua_pushstring(L, ops[attr->op]);
            lua_setfield(L, -2, "op");
            set_string(L, "value", &attr->value);
        }
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "attrs");
    push_pseudos(L, c);
    lua_setfield(L, -2, "pseudos");
}

/* Reference interface: the parse result as a Lua table shaped like the AST
 * of the JavaScript version. It reads the selector without changing it. */
static int to_table_lua(lua_State *L)
{
    const selector_t *sel = selector_check(L, 1);
    size_t i              = 0;
    size_t j              = 0;

    lua_createtable(L, 0, 3);
    push_string(L, &sel->source);
    lua_setfield(L, -2, "source");
    lua_pushboolean(L, sel->has_positional);
    lua_setfield(L, -2, "has_positional");
    lua_createtable(L, (int)sel->ncomplexes, 0);
    for (i = 0; i < sel->ncomplexes; i++) {
        const selector_complex_t *complex = &sel->complexes[i];

        luaL_checkstack(L, 4, "building the selector table");
        lua_createtable(L, 0, 1);
        lua_createtable(L, (int)complex->nparts, 0);
        for (j = 0; j < complex->nparts; j++) {
            const selector_part_t *part = &complex->parts[j];

            lua_createtable(L, 0, 2);
            if (part->combinator != SELECTOR_COMBINATOR_NONE) {
                lua_pushstring(
                    L,
                    part->combinator == SELECTOR_COMBINATOR_CHILD ? ">" : " ");
                lua_setfield(L, -2, "combinator");
            }
            push_compound(L, &part->compound);
            lua_setfield(L, -2, "compound");
            lua_rawseti(L, -2, (int)j + 1);
        }
        lua_setfield(L, -2, "parts");
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "selectors");
    return 1;
}

typedef struct {
    pool_t *pool;
    const ir_t *ir;
    const selector_t *selector;
    const selector_pseudo_t *ancestor;
    int rc;
} resolve_ctx_t;

/* Build the candidate table under the protection of reflow_pcall_ex, which
 * deletes the scratch pool afterwards. Push nothing when the resolution
 * fails, leaving the cause in the context. */
static int resolve_operation(lua_State *L)
{
    resolve_ctx_t *ctx =
        (resolve_ctx_t *)lua_touserdata(L, lua_upvalueindex(1));
    selector_candidates_t cands = {0};
    size_t i                    = 0;

    ctx->rc = selector_exec_resolve(ctx->pool, ctx->ir->index, ctx->selector,
                                    &cands, &ctx->ancestor);
    if (ctx->rc != 0) {
        ctx->rc = errno;
        return 0;
    }
    lua_createtable(L, (int)cands.count, 0);
    for (i = 0; i < cands.count; i++) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L,
                        (lua_Integer)cands.items[i].element->structure.order);
        lua_setfield(L, -2, "order");
        push_pseudos(L, selector_exec_rightmost(cands.items[i].complex));
        lua_setfield(L, -2, "positional");
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

static void resolve_cleanup(lua_State *L, void *ctx)
{
    pool_delete(L, ((resolve_ctx_t *)ctx)->pool);
}

/* Return nil and a selector error with the message at message_idx, whose
 * meta holds the selector source and, when reason is not NULL, the reason and
 * the feature of an unsupported selector. */
static int return_resolve_error(lua_State *L, const selector_t *selector,
                                int message_idx, const char *reason,
                                const char *feature)
{
    lua_createtable(L, 0, 3);
    push_string(L, &selector->source);
    lua_setfield(L, -2, "source");
    if (reason != NULL) {
        lua_pushstring(L, reason);
        lua_setfield(L, -2, "reason");
        lua_pushstring(L, feature);
        lua_setfield(L, -2, "feature");
    }
    push_error(L, RESOLVE_OPERATION, message_idx);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

/* Reference interface: the static candidates of a selector in a compiled
 * template, in document order, shaped like the result of the JavaScript
 * resolveSelector. Each candidate gives the 0-based document order of the
 * element and the positional conditions to confirm at render time. */
static int resolve_lua(lua_State *L)
{
    resolve_ctx_t ctx = {0};
    int status        = LUA_OK;

    ctx.ir       = ir_check(L, 1);
    ctx.selector = selector_check(L, 2);
    lua_settop(L, 2);
    ctx.pool = pool_new(L, 0);
    if (ctx.pool == NULL) {
        lua_pushliteral(L, RESOLVE_OOM_MESSAGE);
        return return_resolve_error(L, ctx.selector, lua_gettop(L), NULL, NULL);
    }
    status = reflow_pcall_ex(L, resolve_operation, &ctx, 0, 1, resolve_cleanup);
    if (status == LUA_OK && ctx.rc == 0) {
        return 1;
    } else if (status != LUA_OK && status != LUA_ERRMEM) {
        return lua_error(L);
    }
    lua_settop(L, 2);
    if (status == LUA_OK && ctx.rc == EINVAL) {
        const char *name = PSEUDO_NAMES[ctx.ancestor->kind];

        lua_pushfstring(L,
                        "positional pseudo-class \":%s\" is only supported on "
                        "the rightmost compound selector",
                        name);
        lua_pushfstring(L, "pseudo-ancestor:%s", name);
        return return_resolve_error(L, ctx.selector, 3, "unsupported",
                                    lua_tostring(L, -1));
    }
    lua_gc(L, LUA_GCCOLLECT, 0);
    lua_pushliteral(L, RESOLVE_OOM_MESSAGE);
    return return_resolve_error(L, ctx.selector, lua_gettop(L), NULL, NULL);
}

LUALIB_API int luaopen_reflow_selector(lua_State *L)
{
    struct luaL_Reg funcs[] = {
        {"compile",     compile_lua    },
        {"is_compiled", is_compiled_lua},
        {"source",      source_lua     },
        {"resolve",     resolve_lua    },
        {NULL,          NULL           },
    };

    reflow_error_require(L, "reflow.error");
    lua_pop(L, 1);
    if (luaL_newmetatable(L, REFLOW_SELECTOR_MT)) {
        lua_pushcfunction(L, selector_gc_lua);
        lua_setfield(L, -2, "__gc");
        lua_pushstring(L, REFLOW_SELECTOR_MT);
        lua_setfield(L, -2, "__name");
        lua_pushstring(L, REFLOW_SELECTOR_MT);
        lua_setfield(L, -2, "__metatable");
        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, to_table_lua);
        lua_setfield(L, -2, "to_table");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
