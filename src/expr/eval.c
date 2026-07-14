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
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

// project
#include "eval.h"
// lua
#include <lauxlib.h>
#include <lua.h>
// system
#include <stdarg.h>
#include <string.h>

/* ============================================================ */
/* Lua version compatibility                                     */
/* ============================================================ */

#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502
static int lua_absindex(lua_State *L, int idx)
{
    return (idx > 0) ? idx : lua_gettop(L) + idx + 1;
}
#endif

static size_t table_len(lua_State *L, int idx)
{
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502
    return lua_objlen(L, idx);
#else
    return (size_t)lua_rawlen(L, idx);
#endif
}

/* ============================================================ */
/* reflow_value → Lua conversion                                */
/* ============================================================ */

void rv_to_lua(lua_State *L, const reflow_value *v)
{
    switch (v->tag) {
    case RV_NULL:
    case RV_UNDEFINED:
        lua_pushnil(L);
        break;
    case RV_NUMBER:
        lua_pushnumber(L, v->number);
        break;
    case RV_STRING:
        lua_pushlstring(L, v->string.data, v->string.len);
        break;
    case RV_BOOL:
        lua_pushboolean(L, v->boolean);
        break;
    case RV_ARRAY:
        lua_createtable(L, (int)v->array.len, 0);
        for (size_t i = 0; i < v->array.len; i++) {
            rv_to_lua(L, &v->array.items[i]);
            lua_rawseti(L, -2, (int)(i + 1));
        }
        break;
    case RV_OBJECT:
        lua_createtable(L, 0, (int)v->object.len);
        for (size_t i = 0; i < v->object.len; i++) {
            rv_prop *p = &v->object.props[i];
            lua_pushlstring(L, p->key, p->key_len);
            rv_to_lua(L, &p->value);
            lua_rawset(L, -3);
        }
        break;
    }
}

/* ============================================================ */
/* Lua → reflow_value conversion (arena-allocated)             */
/* ============================================================ */

static reflow_value lua_to_rv(lua_State *L, int idx, arena_t *arena)
{
    idx = lua_absindex(L, idx);
    int t = lua_type(L, idx);

    switch (t) {
    case LUA_TNIL:
        return rv_undef();
    case LUA_TNUMBER:
        return rv_number(lua_tonumber(L, idx));
    case LUA_TBOOLEAN:
        return rv_bool(lua_toboolean(L, idx));
    case LUA_TSTRING: {
        size_t slen  = 0;
        const char *s = lua_tolstring(L, idx, &slen);
        char *dst = (char *)arena_alloc(arena, slen + 1);
        memcpy(dst, s, slen);
        dst[slen] = '\0';
        return rv_string(dst, slen);
    }
    case LUA_TTABLE: {
        size_t alen = table_len(L, idx);
        if (alen > 0) {
            /* treat as array */
            reflow_value *items = (reflow_value *)
                arena_alloc(arena, alen * sizeof(reflow_value));
            for (size_t i = 0; i < alen; i++) {
                lua_rawgeti(L, idx, (int)(i + 1));
                items[i] = lua_to_rv(L, -1, arena);
                lua_pop(L, 1);
            }
            reflow_value v;
            v.tag          = RV_ARRAY;
            v.array.items  = items;
            v.array.len    = alen;
            v.array.cap    = alen;
            return v;
        }
        /* treat as object */
        lua_pushnil(L);
        size_t n = 0;
        while (lua_next(L, idx)) {
            n++;
            lua_pop(L, 1);
        }
        rv_prop *props = (rv_prop *)
            arena_alloc(arena, n * sizeof(rv_prop));
        n = 0;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            size_t klen = 0;
            const char *k = lua_tolstring(L, -2, &klen);
            char *kdst = (char *)arena_alloc(arena, klen + 1);
            memcpy(kdst, k, klen);
            kdst[klen] = '\0';
            props[n].key     = kdst;
            props[n].key_len = klen;
            props[n].value   = lua_to_rv(L, -1, arena);
            n++;
            lua_pop(L, 1);
        }
        reflow_value v;
        v.tag          = RV_OBJECT;
        v.object.props = props;
        v.object.len   = n;
        v.object.cap   = n;
        return v;
    }
    default:
        /* functions, userdata, threads → undefined */
        return rv_undef();
    }
}

/* ============================================================ */
/* Error helper                                                 */
/* ============================================================ */

static char g_eval_err[256];

static void eval_error(reflow_error *err, const char *msg)
{
    if (err) {
        err->type    = "ReflowRuntimeError";
        err->message = msg;
    }
}

static void eval_errorf(reflow_error *err, const char *fmt, ...)
{
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(g_eval_err, sizeof(g_eval_err), fmt, ap);
        va_end(ap);
        err->type    = "ReflowRuntimeError";
        err->message = g_eval_err;
    }
}

/* ============================================================ */
/* Evaluator                                                    */
/* ============================================================ */

reflow_value expr_eval(const expr_node *node, scope_env *env,
                       lua_State *L, int helpers_ref,
                       arena_t *arena, reflow_error *err)
{
    switch (node->type) {
    case EX_LITERAL:
        return node->literal;

    case EX_DOLLAR: {
        reflow_value *v = scope_resolve_dollar(env);
        return v ? *v : rv_undef();
    }

    case EX_AT: {
        reflow_value *v = scope_resolve_at(env, node->at.name,
                                           strlen(node->at.name));
        return v ? *v : rv_undef();
    }

    case EX_DOT: {
        reflow_value *v = scope_resolve_dot(env, node->dot.name,
                                            strlen(node->dot.name));
        return v ? *v : rv_undef();
    }

    case EX_MEMBER: {
        reflow_value obj = expr_eval(node->member.object, env, L,
                                     helpers_ref, arena, err);
        if (err->message) return rv_undef();

        if (rv_is_nullish(&obj)) {
            if (node->member.optional)
                return rv_undef();
            eval_errorf(err, "cannot read property \"%s\" of %s",
                        node->member.pname,
                        obj.tag == RV_NULL ? "null" : "undefined");
            return rv_undef();
        }
        reflow_value *prop = rv_object_get(&obj, node->member.pname,
                                            strlen(node->member.pname));
        return prop ? *prop : rv_undef();
    }

    case EX_UNARY: {
        reflow_value arg = expr_eval(node->unary.arg, env, L,
                                     helpers_ref, arena, err);
        if (err->message) return rv_undef();
        return rv_bool(!rv_is_truthy(&arg));
    }

    case EX_BINARY: {
        /* && and || need short-circuit (don't eval right early) */
        if (node->binary.op == OP_AND) {
            reflow_value l = expr_eval(node->binary.left, env, L,
                                       helpers_ref, arena, err);
            if (err->message) return rv_undef();
            if (rv_is_falsy(&l)) return l;
            return expr_eval(node->binary.right, env, L,
                             helpers_ref, arena, err);
        }
        if (node->binary.op == OP_OR) {
            reflow_value l = expr_eval(node->binary.left, env, L,
                                       helpers_ref, arena, err);
            if (err->message) return rv_undef();
            if (rv_is_truthy(&l)) return l;
            return expr_eval(node->binary.right, env, L,
                             helpers_ref, arena, err);
        }
        if (node->binary.op == OP_COALESCE) {
            reflow_value l = expr_eval(node->binary.left, env, L,
                                       helpers_ref, arena, err);
            if (err->message) return rv_undef();
            if (!rv_is_nullish(&l)) return l;
            return expr_eval(node->binary.right, env, L,
                             helpers_ref, arena, err);
        }

        /* non-short-circuit: evaluate both sides */
        reflow_value l = expr_eval(node->binary.left, env, L,
                                   helpers_ref, arena, err);
        if (err->message) return rv_undef();
        reflow_value r = expr_eval(node->binary.right, env, L,
                                   helpers_ref, arena, err);
        if (err->message) return rv_undef();

        switch (node->binary.op) {
        case OP_EQ: return rv_bool(rv_strict_eq(&l, &r));
        case OP_NE: return rv_bool(rv_strict_neq(&l, &r));
        case OP_LT: {
            int c = rv_compare(&l, &r);
            return rv_bool(c == 2 ? 0 : c < 0);
        }
        case OP_GT: {
            int c = rv_compare(&l, &r);
            return rv_bool(c == 2 ? 0 : c > 0);
        }
        case OP_LE: {
            int c = rv_compare(&l, &r);
            return rv_bool(c == 2 ? 0 : c <= 0);
        }
        case OP_GE: {
            int c = rv_compare(&l, &r);
            return rv_bool(c == 2 ? 0 : c >= 0);
        }
        default:
            eval_error(err, "unknown binary operator");
            return rv_undef();
        }
    }

    case EX_TERNARY: {
        reflow_value test = expr_eval(node->ternary.test, env, L,
                                      helpers_ref, arena, err);
        if (err->message) return rv_undef();
        if (rv_is_truthy(&test))
            return expr_eval(node->ternary.cons, env, L,
                             helpers_ref, arena, err);
        return expr_eval(node->ternary.alt, env, L,
                         helpers_ref, arena, err);
    }

    case EX_CALL: {
        if (!L || helpers_ref == LUA_NOREF) {
            eval_errorf(err, "helper \"%s\" is not available "
                        "(no helper registry)", node->call.callee);
            return rv_undef();
        }

        /* Look up helper function */
        lua_rawgeti(L, LUA_REGISTRYINDEX, helpers_ref);
        lua_getfield(L, -1, node->call.callee);
        lua_remove(L, -2); /* remove helpers table */

        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            eval_errorf(err, "helper \"%s\" is not registered",
                        node->call.callee);
            return rv_undef();
        }

        /* Evaluate and push arguments */
        for (size_t i = 0; i < node->call.n_args; i++) {
            reflow_value arg = expr_eval(node->call.args[i], env, L,
                                         helpers_ref, arena, err);
            if (err->message) {
                lua_pop(L, 1 + (int)i); /* function + pushed args */
                return rv_undef();
            }
            rv_to_lua(L, &arg);
        }

        /* Call helper */
        if (lua_pcall(L, (int)node->call.n_args, 1, 0) != 0) {
            const char *m = lua_tostring(L, -1);
            eval_errorf(err, "helper \"%s\": %s",
                        node->call.callee, m ? m : "error");
            lua_pop(L, 1);
            return rv_undef();
        }

        reflow_value result = lua_to_rv(L, -1, arena);
        lua_pop(L, 1);
        return result;
    }

    case EX_OBJECT: {
        rv_prop *props = (rv_prop *)
            arena_alloc(arena, node->object.n * sizeof(rv_prop));
        for (size_t i = 0; i < node->object.n; i++) {
            obj_entry *e = &node->object.entries[i];
            if (e->computed) {
                reflow_value kv = expr_eval(e->key_expr, env, L,
                                            helpers_ref, arena, err);
                if (err->message) return rv_undef();
                /* key must be string or number */
                if (kv.tag == RV_STRING) {
                    props[i].key = kv.string.data;
                    props[i].key_len = kv.string.len;
                } else if (kv.tag == RV_NUMBER) {
                    char buf[32];
                    size_t len = number_to_string(kv.number, buf);
                    char *dst = (char *)arena_alloc(arena, len + 1);
                    memcpy(dst, buf, len);
                    dst[len] = '\0';
                    props[i].key = dst;
                    props[i].key_len = len;
                } else {
                    eval_error(err, "computed key must evaluate to a string or number");
                    return rv_undef();
                }
            } else {
                props[i].key = e->key;
                props[i].key_len = e->klen;
            }
            props[i].value = expr_eval(e->val, env, L,
                                       helpers_ref, arena, err);
            if (err->message) return rv_undef();
        }
        reflow_value v;
        v.tag          = RV_OBJECT;
        v.object.props = props;
        v.object.len   = node->object.n;
        v.object.cap   = node->object.n;
        return v;
    }

    case EX_ARRAY: {
        reflow_value *items = (reflow_value *)
            arena_alloc(arena, node->array.n * sizeof(reflow_value));
        for (size_t i = 0; i < node->array.n; i++) {
            items[i] = expr_eval(node->array.items[i], env, L,
                                 helpers_ref, arena, err);
            if (err->message) return rv_undef();
        }
        reflow_value v;
        v.tag         = RV_ARRAY;
        v.array.items = items;
        v.array.len   = node->array.n;
        v.array.cap   = node->array.n;
        return v;
    }

    default:
        eval_error(err, "unknown expression node type");
        return rv_undef();
    }
}

/* ============================================================ */
/* collectHelperNames                                           */
/* ============================================================ */

void expr_collect_helpers(const expr_node *node,
                          void (*cb)(const char *, size_t, void *),
                          void *ctx)
{
    if (!node) return;
    switch (node->type) {
    case EX_CALL:
        cb(node->call.callee, strlen(node->call.callee), ctx);
        for (size_t i = 0; i < node->call.n_args; i++)
            expr_collect_helpers(node->call.args[i], cb, ctx);
        break;
    case EX_MEMBER:
        expr_collect_helpers(node->member.object, cb, ctx);
        break;
    case EX_UNARY:
        expr_collect_helpers(node->unary.arg, cb, ctx);
        break;
    case EX_BINARY:
        expr_collect_helpers(node->binary.left, cb, ctx);
        expr_collect_helpers(node->binary.right, cb, ctx);
        break;
    case EX_TERNARY:
        expr_collect_helpers(node->ternary.test, cb, ctx);
        expr_collect_helpers(node->ternary.cons, cb, ctx);
        expr_collect_helpers(node->ternary.alt, cb, ctx);
        break;
    case EX_OBJECT:
        for (size_t i = 0; i < node->object.n; i++) {
            if (node->object.entries[i].computed)
                expr_collect_helpers(node->object.entries[i].key_expr,
                                     cb, ctx);
            expr_collect_helpers(node->object.entries[i].val, cb, ctx);
        }
        break;
    case EX_ARRAY:
        for (size_t i = 0; i < node->array.n; i++)
            expr_collect_helpers(node->array.items[i], cb, ctx);
        break;
    default:
        break;
    }
}
