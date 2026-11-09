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

#ifndef REFLOW_SELECTOR_COMPILE_H
#define REFLOW_SELECTOR_COMPILE_H

#include <stddef.h>

#include <lua.h>

#include "pool.h"
#include "selector.h"

/* Where a selector failed to parse. */
typedef struct selector_parse_error_t {
    int unsupported; /* 1 for an unsupported construct, 0 for bad syntax */
    size_t pos;      /* 0-based byte offset in the source */
} selector_parse_error_t;

/**
 * Parse sel->source, which must stay alive as long as sel, into sel, taking
 * every condition from pool. The grammar has no nested constructs, so the
 * source is scanned once from left to right without recursion.
 *
 * @return 0 on success; 1 when the source is not a supported selector, with
 *         err set and the message and the feature (a string, or nil when
 *         none applies) pushed; or -1 with errno set to ENOMEM when the pool
 *         runs out of memory. Lua memory errors are raised.
 */
int selector_parse(lua_State *L, pool_t *pool, selector_t *sel,
                   selector_parse_error_t *err);

#endif /* REFLOW_SELECTOR_COMPILE_H */
