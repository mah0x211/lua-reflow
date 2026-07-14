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
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef REFLOW_SELECTOR_RESOLVE_H
#define REFLOW_SELECTOR_RESOLVE_H

#include <lua.h>
#include <stdbool.h>
#include <stddef.h>
#include "../compile_arena.h"
#include "../error.h"
#include "../ir.h"
#include "index.h"
#include "parse.h"

typedef struct {
    const ir_node         *element;
    const sel_pseudo_cond *positional;   /* NULL when n_positional == 0 */
    size_t                 n_positional;
} sel_candidate;

typedef struct {
    sel_candidate *items;
    size_t         count;
} sel_candidates;

/*
 * Right-to-left resolution: return the ordered list of candidates from
 * `index` that match `selector`. Duplicates across the selector list
 * are removed by element identity; results are sorted by document order.
 *
 * Positional pseudo-classes (`:first-child`, `:nth-*`, ...) do not
 * short-circuit resolution — they are attached to each candidate as
 * predicates the render step evaluates once actual sibling positions
 * are known.
 *
 * Returns a candidates array bump-allocated from `arena`. On rejection
 * (positional pseudos on a non-rightmost compound) returns NULL and
 * fills err with type = "ReflowSelectorError", reason = "unsupported",
 * feature = "pseudo-ancestor:<name>".
 */
sel_candidates *sel_resolve(compile_arena *arena, lua_State *L,
                            const sel_index *index,
                            const sel_compiled *selector,
                            reflow_error *err);

/*
 * Runtime evaluation of one positional pseudo condition against a
 * sibling emission position. `index` and `total` are 1-based counts of
 * position among ALL siblings; `of_type_index` and `of_type_total`
 * count only siblings that share the element's tag name.
 */
bool sel_eval_positional(const sel_pseudo_cond *cond,
                         size_t index, size_t total,
                         size_t of_type_index, size_t of_type_total);

#endif /* REFLOW_SELECTOR_RESOLVE_H */
