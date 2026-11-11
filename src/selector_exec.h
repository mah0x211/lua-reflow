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

#ifndef REFLOW_SELECTOR_EXEC_H
#define REFLOW_SELECTOR_EXEC_H

#include <stddef.h>

#include "ir.h"
#include "pool.h"
#include "selector.h"

/* An element whose static conditions match a complex selector. The
 * positional conditions to confirm at render time are those of the rightmost
 * compound of complex. */
typedef struct selector_candidate_t {
    const ir_element_t *element;
    const selector_complex_t *complex;
} selector_candidate_t;

/* Candidates in document order, each element once. items is NULL when count
 * is 0. */
typedef struct selector_candidates_t {
    selector_candidate_t *items;
    size_t count;
} selector_candidates_t;

/* Return the rightmost compound of a complex selector. */
static inline const selector_compound_t *
selector_exec_rightmost(const selector_complex_t *complex)
{
    return &complex->parts[complex->nparts - 1].compound;
}

/**
 * Resolve the static candidates of a selector against the DOM index of a
 * compiled template, as the JavaScript resolveSelector does: pick the seed
 * list of each complex selector from the index, match its static conditions
 * and walk the ancestors for the preceding parts, then merge the results in
 * document order. When an element matches several complex selectors, the
 * first one is kept. Positional conditions are not matched here.
 *
 * The candidate array is allocated from pool and owned by the caller. The
 * walk uses no C recursion.
 *
 * @param ancestor Set to the first positional condition of a part other than
 *                 the rightmost one when the call fails with EINVAL.
 * @return 0 on success, or -1 with errno set: EINVAL when a complex selector
 *         that has a positional condition on a part other than the rightmost
 *         one matches an element, or ENOMEM. out is left unchanged on
 *         failure.
 */
int selector_exec_resolve(pool_t *pool, const ir_dom_index_t *index,
                          const selector_t *selector,
                          selector_candidates_t *out,
                          const selector_pseudo_t **ancestor);

#endif /* REFLOW_SELECTOR_EXEC_H */
