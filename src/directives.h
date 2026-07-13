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

#ifndef REFLOW_DIRECTIVES_H
#define REFLOW_DIRECTIVES_H

#include <stdbool.h>
#include <stddef.h>
#include "arena.h"
#include "compile_arena.h"
#include "error.h"
#include "ir.h"
#include "value.h"

/*
 * All parsers allocate from compile_arena.
 * On error: return NULL (or -1) and set err.
 * directive_name is used for error messages.
 */

/* x-data: wrap value in {}, parse as JSON5 → reflow_value (RV_OBJECT). */
reflow_value *directives_parse_data(compile_arena *arena, lua_State *L,
                                    arena_t *rarena,
                                    const char *value, size_t len,
                                    reflow_error *err);

/* x-with: parse "name = expr, ..." → ir_with_binding array. */
typedef struct {
    ir_with_binding *bindings;
    size_t           n;
} ir_with_result;

ir_with_result directives_parse_with(compile_arena *arena, lua_State *L,
                                     const char *value, size_t len,
                                     reflow_error *err);

/* x-if / x-elseif / x-text / x-html / x-include / x-match / x-case /
 * x-break-if: parse expression value → expr_node. */
expr_node *directives_parse_expr(compile_arena *arena, lua_State *L,
                                 const char *value, size_t len,
                                 const char *directive_name,
                                 reflow_error *err);

/* x-else / x-nocase / x-break: assert value is empty. */
int directives_assert_empty(const char *value, size_t len,
                            const char *directive_name,
                            reflow_error *err);

/* x-for: parse "var = start, stop[, step]" → ir_for_spec. */
ir_for_spec *directives_parse_for(compile_arena *arena, lua_State *L,
                                  const char *value, size_t len,
                                  reflow_error *err);

/* x-each: parse "item[, index] in collection" → ir_each_spec. */
ir_each_spec *directives_parse_each(compile_arena *arena, lua_State *L,
                                    const char *value, size_t len,
                                    reflow_error *err);

/* Check if name is a known directive (excluding x-bind:* prefix). */
bool directives_is_known(const char *name);

/* Classify directive by group: 'D','W','S','I','C','A','K', or 0. */
char directives_group(const char *name);

#endif /* REFLOW_DIRECTIVES_H */
