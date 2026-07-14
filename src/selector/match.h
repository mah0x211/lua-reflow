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

#ifndef REFLOW_SELECTOR_MATCH_H
#define REFLOW_SELECTOR_MATCH_H

#include <stdbool.h>
#include "../ir.h"
#include "parse.h"

/*
 * Return true when `el` satisfies every non-positional condition of
 * `compound` — tag, id, classes, and attribute conditions.  Positional
 * pseudo-classes are intentionally excluded: they depend on runtime
 * sibling order and are evaluated separately by the interpreter.
 */
bool sel_match_compound(const ir_node *el, const sel_compound *compound);

/*
 * Evaluate a single attribute condition against the given value (may
 * be NULL when the attribute is absent).  The result mirrors the CSS
 * Level 3 semantics exactly.
 */
bool sel_match_attr(const char *value, const sel_attr_cond *cond);

#endif /* REFLOW_SELECTOR_MATCH_H */
