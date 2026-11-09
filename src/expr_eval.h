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

#ifndef REFLOW_EXPR_EVAL_H
#define REFLOW_EXPR_EVAL_H

#include <stddef.h>

#include <lua.h>

#include "expr.h"
#include "pool.h"
#include "scope.h"

typedef struct expr_eval_frame_t expr_eval_frame_t;

/* The evaluator of one render. The frame array grows in the render's pool
 * and is reused by later evaluations. */
typedef struct expr_eval_t {
    lua_State *L;
    pool_t *pool;
    const scope_t *scope;
    int helpers_idx; /* the helpers table, or 0 when there is none */
    expr_eval_frame_t *frames;
    size_t capacity;
    /* Set when a runtime error was raised by the renderer itself, so that
     * the error value is a message string rather than a value raised by a
     * helper or a metamethod. */
    int raised;
} expr_eval_t;

void expr_eval_init(expr_eval_t *ev, lua_State *L, pool_t *pool,
                    const scope_t *scope, int helpers_idx);

/**
 * Evaluate an expression without C recursion and push its value. Values
 * being evaluated are kept on the Lua stack above the current top.
 *
 * @return 0 with the value pushed, or -1 with the stack unchanged when the
 *         pool runs out of memory. A runtime error raises a Lua error: a
 *         message string with ev->raised set, or the value raised by a
 *         helper or a metamethod.
 */
int expr_eval(expr_eval_t *ev, const reflow_expression_t *expression);

/* Raise the message string on top of the stack as a runtime error of the
 * renderer. */
int expr_eval_error(expr_eval_t *ev);

#endif /* REFLOW_EXPR_EVAL_H */
