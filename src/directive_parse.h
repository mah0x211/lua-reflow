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

/* When message_pushed is non-zero, message points to a Lua string that the
 * parser left at the top of the stack; the caller owns that stack slot. */
typedef struct directive_parse_error_t {
    const char *message;
    const char *cause;
    size_t position;
    int message_pushed;
    /* When expression is not NULL, the error points at byte offset
     * expression_pos in [expression, expression + expression_len). */
    const char *expression;
    size_t expression_len;
    size_t expression_pos;
} directive_parse_error_t;

/* Helper names registered for the compilation. The strings are borrowed from
 * the caller and must outlive the compilation. */
typedef struct directive_helper_name_t {
    const char *name;
    size_t len;
} directive_helper_name_t;

typedef struct directive_parse_ctx_t {
    lua_State *L;
    ir_t *ir;
    ir_element_t *element;
    const char *attribute;
    size_t attribute_len;
    const directive_helper_name_t *helpers;
    size_t helper_count;
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

/**
 * Validate the directive combination of an element after all of its
 * attributes have been parsed.
 *
 * @return 0 when valid, or -1 with errno and error set.
 */
int directive_validate_element(const directive_parse_ctx_t *ctx,
                               directive_parse_error_t *error);

#endif /* REFLOW_DIRECTIVE_PARSE_H */
