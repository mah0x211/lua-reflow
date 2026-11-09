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

#ifndef REFLOW_STRUCTURE_H
#define REFLOW_STRUCTURE_H

#include "ir.h"

typedef enum {
    STRUCTURE_FOUND_NONE = 0,
    STRUCTURE_FOUND_TEXT,
    STRUCTURE_FOUND_CHAIN,
} structure_found_t;

typedef struct structure_error_t {
    const char *message;
    const ir_element_t *element;
    /* What an x-match element found in place of an x-case / x-nocase child.
     * For STRUCTURE_FOUND_TEXT, text holds the leading 20 UTF-16 code units
     * of that text (an astral character is never split). */
    structure_found_t found;
    const char *text;
    size_t text_len;
} structure_error_t;

/**
 * Finalize the children of a root or element node once it is closed: merge
 * x-if / x-elseif / x-else siblings into chain nodes, move the x-case /
 * x-nocase children of an x-match element into its match branches, and
 * reject branch markers that are left outside their chain or x-match.
 *
 * @return 0 on success, or -1 with errno set. On EINVAL, error names the
 *         message and the offending element.
 */
int structure_close_children(ir_t *ir, ir_node_t *parent,
                             structure_error_t *error);

#endif /* REFLOW_STRUCTURE_H */
