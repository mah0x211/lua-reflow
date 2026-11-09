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

#include <limits.h>
#include <stdint.h>

#include <lauxlib.h>

#include "expr_eval.h"
#include "value_lua.h"

#define EXPR_EVAL_INITIAL_FRAMES 16
/* The most values one evaluation step pushes, plus one slot so that the
 * error message can still be pushed when the stack cannot grow. */
#define EXPR_EVAL_STEP_SLOTS     5

/* One expression node being evaluated. stage counts the operands already
 * evaluated; item and entry are the next array item or argument and the
 * current object entry. */
struct expr_eval_frame_t {
    const reflow_expr_t *node;
    const reflow_expr_t *item;
    const reflow_expr_object_entry_t *entry;
    size_t count;
    int stage;
};

void expr_eval_init(expr_eval_t *ev, lua_State *L, pool_t *pool,
                    const scope_t *scope, int helpers_idx)
{
    *ev = (expr_eval_t){
        .L           = L,
        .pool        = pool,
        .scope       = scope,
        .helpers_idx = helpers_idx,
    };
}

int expr_eval_error(expr_eval_t *ev)
{
    ev->raised = 1;
    return lua_error(ev->L);
}

/* Decode one UTF-8 code point at *pos and advance past it. A byte that does
 * not start a valid sequence is returned as its own value, so any byte
 * string has a deterministic order. */
static uint32_t next_code_point(const unsigned char *s, size_t len, size_t *pos)
{
    unsigned char c = s[*pos];
    uint32_t cp     = 0;
    uint32_t min    = 0;
    size_t need     = 0;
    size_t i        = 0;

    if (c < 0x80) {
        (*pos)++;
        return c;
    } else if ((c & 0xe0) == 0xc0) {
        need = 1, cp = c & 0x1f, min = 0x80;
    } else if ((c & 0xf0) == 0xe0) {
        need = 2, cp = c & 0x0f, min = 0x800;
    } else if ((c & 0xf8) == 0xf0) {
        need = 3, cp = c & 0x07, min = 0x10000;
    } else {
        (*pos)++;
        return c;
    }
    if (len - *pos - 1 < need) {
        (*pos)++;
        return c;
    }
    for (i = 1; i <= need; i++) {
        unsigned char b = s[*pos + i];
        if ((b & 0xc0) != 0x80) {
            (*pos)++;
            return c;
        }
        cp = (cp << 6) | (b & 0x3f);
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        (*pos)++;
        return c;
    }
    *pos += need + 1;
    return cp;
}

/* Compare two UTF-8 strings in the order of their UTF-16 code units, as
 * JavaScript compares strings. The order differs from the code point order
 * only between a supplementary character and U+E000..U+FFFF. */
static int compare_utf16(const char *a, size_t alen, const char *b, size_t blen)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    size_t i               = 0;
    size_t j               = 0;

    while (i < alen && j < blen) {
        uint32_t ca = next_code_point(x, alen, &i);
        uint32_t cb = next_code_point(y, blen, &j);
        uint32_t ua = ca;
        uint32_t ub = cb;

        if (ca == cb) {
            continue;
        }
        if (ca >= 0x10000) {
            ua = 0xd800 + ((ca - 0x10000) >> 10);
        }
        if (cb >= 0x10000) {
            ub = 0xd800 + ((cb - 0x10000) >> 10);
        }
        if (ua == ub) {
            /* Both are supplementary with the same high surrogate. */
            ua = 0xdc00 + ((ca - 0x10000) & 0x3ff);
            ub = 0xdc00 + ((cb - 0x10000) & 0x3ff);
        }
        return ua < ub ? -1 : 1;
    }
    if (i < alen) {
        return 1;
    } else if (j < blen) {
        return -1;
    }
    return 0;
}

static const char *operator_name(reflow_expr_operator_t op)
{
    switch (op) {
    case REFLOW_EXPR_LT:
        return "<";
    case REFLOW_EXPR_GT:
        return ">";
    case REFLOW_EXPR_LE:
        return "<=";
    case REFLOW_EXPR_GE:
        return ">=";
    case REFLOW_EXPR_NOT:
    case REFLOW_EXPR_EQ:
    case REFLOW_EXPR_NE:
    case REFLOW_EXPR_AND:
    case REFLOW_EXPR_OR:
    case REFLOW_EXPR_COALESCE:
        break;
    }
    return "?";
}

static int order_matches(reflow_expr_operator_t op, int order)
{
    switch (op) {
    case REFLOW_EXPR_LT:
        return order < 0;
    case REFLOW_EXPR_GT:
        return order > 0;
    case REFLOW_EXPR_LE:
        return order <= 0;
    case REFLOW_EXPR_GE:
        return order >= 0;
    case REFLOW_EXPR_NOT:
    case REFLOW_EXPR_EQ:
    case REFLOW_EXPR_NE:
    case REFLOW_EXPR_AND:
    case REFLOW_EXPR_OR:
    case REFLOW_EXPR_COALESCE:
        break;
    }
    return 0;
}

/* Compare the operands at -2 and -1. Only two numbers or two strings can be
 * ordered; any other pair is a runtime error. */
static int compare_operands(expr_eval_t *ev, reflow_expr_operator_t op)
{
    lua_State *L = ev->L;
    int left     = lua_type(L, -2);
    int right    = lua_type(L, -1);

    if (left == LUA_TNUMBER && right == LUA_TNUMBER) {
        lua_Number a = lua_tonumber(L, -2);
        lua_Number b = lua_tonumber(L, -1);

        switch (op) {
        case REFLOW_EXPR_LT:
            return a < b;
        case REFLOW_EXPR_GT:
            return a > b;
        case REFLOW_EXPR_LE:
            return a <= b;
        case REFLOW_EXPR_GE:
            return a >= b;
        case REFLOW_EXPR_NOT:
        case REFLOW_EXPR_EQ:
        case REFLOW_EXPR_NE:
        case REFLOW_EXPR_AND:
        case REFLOW_EXPR_OR:
        case REFLOW_EXPR_COALESCE:
            break;
        }
        return 0;
    } else if (left == LUA_TSTRING && right == LUA_TSTRING) {
        size_t alen   = 0;
        size_t blen   = 0;
        const char *a = lua_tolstring(L, -2, &alen);
        const char *b = lua_tolstring(L, -1, &blen);

        return order_matches(op, compare_utf16(a, alen, b, blen));
    }
    lua_pushfstring(L, "cannot compare %s with %s using \"%s\"",
                    value_lua_type_name(L, -2), value_lua_type_name(L, -1),
                    operator_name(op));
    return expr_eval_error(ev);
}

/* Replace the object on top of the stack with its property. */
static void read_member(expr_eval_t *ev, const reflow_expr_t *node)
{
    lua_State *L                         = ev->L;
    const reflow_expr_string_t *property = &node->as.member.property;

    if (value_lua_is_nullish(L, -1)) {
        if (node->as.member.optional) {
            lua_pop(L, 1);
            lua_pushnil(L);
            return;
        }
        lua_pushfstring(L, "Cannot read properties of %s (reading '",
                        lua_isnil(L, -1) ? "undefined" : "null");
        lua_pushlstring(L, property->data, property->len);
        lua_pushliteral(L, "')");
        lua_concat(L, 3);
        (void)expr_eval_error(ev);
    } else if (lua_type(L, -1) == LUA_TTABLE) {
        lua_pushlstring(L, property->data, property->len);
        lua_gettable(L, -2);
        lua_remove(L, -2);
    } else {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
}

/* Push the helper function called by node. */
static void push_helper(expr_eval_t *ev, const reflow_expr_t *node)
{
    lua_State *L                       = ev->L;
    const reflow_expr_string_t *callee = &node->as.call.callee;

    if (ev->helpers_idx != 0) {
        lua_pushlstring(L, callee->data, callee->len);
        lua_rawget(L, ev->helpers_idx);
        if (lua_type(L, -1) == LUA_TFUNCTION) {
            return;
        }
        lua_pop(L, 1);
    }
    lua_pushliteral(L, "helper \"");
    lua_pushlstring(L, callee->data, callee->len);
    lua_pushliteral(L, "\" is not registered");
    lua_concat(L, 3);
    (void)expr_eval_error(ev);
}

/* Turn the object key on top of the stack into a string key: a number
 * becomes the string JavaScript gives it. */
static void normalize_key(expr_eval_t *ev)
{
    lua_State *L = ev->L;

    switch (lua_type(L, -1)) {
    case LUA_TSTRING:
        return;
    case LUA_TNUMBER:
        value_lua_push_number_string(L, -1);
        lua_replace(L, -2);
        return;
    default:
        lua_pushfstring(L,
                        "object literal: computed key must evaluate to a "
                        "string or number, got %s",
                        value_lua_type_name(L, -1));
        (void)expr_eval_error(ev);
    }
}

static int size_hint(size_t count)
{
    return count > INT_MAX ? INT_MAX : (int)count;
}

static int checked_index(lua_State *L, size_t index)
{
    if (index > INT_MAX) {
        return luaL_error(L, "array literal is too large");
    }
    return (int)index;
}

static int enter(expr_eval_t *ev, size_t *depth, const reflow_expr_t *node)
{
    if (*depth == ev->capacity) {
        size_t capacity =
            ev->capacity ? ev->capacity * 2 : EXPR_EVAL_INITIAL_FRAMES;
        expr_eval_frame_t *frames = NULL;

        if (capacity > SIZE_MAX / sizeof(*frames)) {
            return -1;
        }
        frames = (expr_eval_frame_t *)pool_realloc(ev->pool, ev->frames,
                                                   capacity * sizeof(*frames));
        if (frames == NULL) {
            return -1;
        }
        ev->frames   = frames;
        ev->capacity = capacity;
    }
    ev->frames[(*depth)++] = (expr_eval_frame_t){
        .node = node,
    };
    return 0;
}

/* Advance the binary operator in frame by one stage. Return 1 when the
 * result is on top of the stack. */
static int step_binary(expr_eval_t *ev, expr_eval_frame_t *frame,
                       const reflow_expr_t **next)
{
    lua_State *L              = ev->L;
    const reflow_expr_t *node = frame->node;
    reflow_expr_operator_t op = node->as.binary.op;
    int stage                 = frame->stage++;

    if (stage == 0) {
        *next = node->as.binary.left;
        return 0;
    }
    switch (op) {
    case REFLOW_EXPR_AND:
    case REFLOW_EXPR_OR:
    case REFLOW_EXPR_COALESCE:
        if (stage == 1) {
            int keep_left = op == REFLOW_EXPR_AND ?
                                !value_lua_is_truthy(L, -1) :
                            op == REFLOW_EXPR_OR ? value_lua_is_truthy(L, -1) :
                                                   !value_lua_is_nullish(L, -1);
            if (keep_left) {
                return 1;
            }
            lua_pop(L, 1);
            *next = node->as.binary.right;
            return 0;
        }
        return 1;
    case REFLOW_EXPR_EQ:
    case REFLOW_EXPR_NE:
    case REFLOW_EXPR_LT:
    case REFLOW_EXPR_GT:
    case REFLOW_EXPR_LE:
    case REFLOW_EXPR_GE:
    case REFLOW_EXPR_NOT:
        break;
    }
    if (stage == 1) {
        *next = node->as.binary.right;
        return 0;
    } else {
        int result = 0;

        if (op == REFLOW_EXPR_EQ || op == REFLOW_EXPR_NE) {
            result = lua_rawequal(L, -2, -1) == (op == REFLOW_EXPR_EQ);
        } else {
            result = compare_operands(ev, op);
        }
        lua_pop(L, 2);
        lua_pushboolean(L, result);
        return 1;
    }
}

/* Advance the node in frame by one stage. Set *next to a child to evaluate
 * first, or return 1 when the node's value is on top of the stack. */
static int step(expr_eval_t *ev, expr_eval_frame_t *frame,
                const reflow_expr_t **next)
{
    lua_State *L              = ev->L;
    const reflow_expr_t *node = frame->node;

    switch (node->type) {
    case REFLOW_EXPR_LITERAL:
        if (value_lua_push(L, ev->pool, node->as.literal) != 0) {
            return -1;
        }
        return 1;
    case REFLOW_EXPR_DOLLAR:
        scope_push_dollar(ev->scope);
        return 1;
    case REFLOW_EXPR_AT:
        scope_push_at(ev->scope, node->as.scope.name.data,
                      node->as.scope.name.len);
        return 1;
    case REFLOW_EXPR_DOT:
        scope_push_dot(ev->scope, node->as.scope.name.data,
                       node->as.scope.name.len);
        return 1;
    case REFLOW_EXPR_MEMBER:
        if (frame->stage++ == 0) {
            *next = node->as.member.object;
            return 0;
        }
        read_member(ev, node);
        return 1;
    case REFLOW_EXPR_UNARY:
        if (frame->stage++ == 0) {
            *next = node->as.unary.argument;
            return 0;
        } else {
            int result = !value_lua_is_truthy(L, -1);
            lua_pop(L, 1);
            lua_pushboolean(L, result);
            return 1;
        }
    case REFLOW_EXPR_BINARY:
        return step_binary(ev, frame, next);
    case REFLOW_EXPR_TERNARY:
        switch (frame->stage++) {
        case 0:
            *next = node->as.ternary.test;
            return 0;
        case 1: {
            int test = value_lua_is_truthy(L, -1);
            lua_pop(L, 1);
            *next =
                test ? node->as.ternary.consequent : node->as.ternary.alternate;
            return 0;
        }
        default:
            return 1;
        }
    case REFLOW_EXPR_CALL:
        if (frame->stage == 0) {
            frame->stage = 1;
            push_helper(ev, node);
            frame->item = node->as.call.arguments.head;
        }
        if (frame->item != NULL) {
            *next       = frame->item;
            frame->item = frame->item->next;
            frame->count++;
            return 0;
        } else if (frame->count > INT_MAX) {
            lua_pushliteral(L, "too many helper arguments");
            return expr_eval_error(ev);
        }
        lua_call(L, (int)frame->count, 1);
        return 1;
    case REFLOW_EXPR_ARRAY:
        if (frame->stage == 0) {
            frame->stage = 1;
            lua_createtable(L, size_hint(node->as.array.count), 0);
            frame->item = node->as.array.head;
        } else {
            lua_rawseti(L, -2, checked_index(L, ++frame->count));
        }
        if (frame->item != NULL) {
            *next       = frame->item;
            frame->item = frame->item->next;
            return 0;
        }
        return 1;
    case REFLOW_EXPR_OBJECT:
        switch (frame->stage) {
        case 0:
            lua_createtable(L, 0, size_hint(node->as.object.count));
            frame->entry = node->as.object.head;
            break;
        case 1:
            /* The key is on top: evaluate the value next. */
            normalize_key(ev);
            frame->stage = 2;
            *next        = frame->entry->value;
            return 0;
        default:
            lua_rawset(L, -3);
            frame->entry = frame->entry->next;
            break;
        }
        if (frame->entry == NULL) {
            return 1;
        }
        frame->stage = 1;
        *next        = frame->entry->key;
        return 0;
    }
    return luaL_error(L, "unsupported expression type");
}

int expr_eval(expr_eval_t *ev, const reflow_expression_t *expression)
{
    lua_State *L = ev->L;
    int top      = lua_gettop(L);
    size_t depth = 0;

    if (enter(ev, &depth, expression->root) != 0) {
        return -1;
    }
    while (depth > 0) {
        const reflow_expr_t *next = NULL;
        int done                  = 0;

        if (!lua_checkstack(L, EXPR_EVAL_STEP_SLOTS)) {
            lua_pushliteral(L, "expression is too deeply nested");
            return expr_eval_error(ev);
        }
        done = step(ev, &ev->frames[depth - 1], &next);
        if (done < 0 || (next != NULL && enter(ev, &depth, next) != 0)) {
            lua_settop(L, top);
            return -1;
        } else if (done) {
            depth--;
        }
    }
    return 0;
}
