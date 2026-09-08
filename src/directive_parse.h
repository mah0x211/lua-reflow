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

#ifndef REFLOW_DIRECTIVE_PARSE_H
#define REFLOW_DIRECTIVE_PARSE_H

#include <lua.h>
#include <stddef.h>

#include "ir.h"

typedef struct directive_parse_error_t {
    const char *message;
    const char *cause;
    size_t position;
} directive_parse_error_t;

typedef struct directive_parse_ctx_t {
    lua_State *L;
    ir_t *ir;
    ir_element_t *element;
} directive_parse_ctx_t;

/**
 * Parse and attach one directive attribute.
 *
 * @return 0 when handled, 1 when no registered parser matches the suffix, or
 *         -1 with errno and error set when a matched directive is invalid.
 */
int directive_parse_attribute(const directive_parse_ctx_t *ctx,
                              const char *suffix, size_t suffix_len,
                              const char *value, size_t value_len,
                              directive_parse_error_t *error);

#endif /* REFLOW_DIRECTIVE_PARSE_H */
