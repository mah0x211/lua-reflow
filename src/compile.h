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

#ifndef REFLOW_COMPILE_H
#define REFLOW_COMPILE_H

#include <stddef.h>
#include "compile_arena.h"
#include "error.h"
#include "ir.h"

/*
 * Compile an HTML template into an IR tree.
 *
 * Runs the SAX parser (parser.c) over `html` and builds an IR forest whose
 * root is IR_ROOT. Attributes are split by `prefix` (typically "x-"):
 * regular attributes stay on element.attrs verbatim, while known x-*
 * directives are parsed by the directives module and stored on
 * element.directives. Helper references are validated against the
 * `helper_names` set (array of C strings) and unknown x-* directives are
 * rejected.
 *
 * All IR memory is allocated from `arena`; the caller keeps the arena alive
 * for as long as the returned tree must be usable.
 *
 * Returns the root ir_node on success, or NULL on error (with `err` set).
 */
ir_node *compile_template(compile_arena *arena, lua_State *L,
                          const char *template_name,
                          const char *html, size_t html_len,
                          const char *prefix, size_t prefix_len,
                          const char **helper_names, size_t n_helpers,
                          reflow_error *err);

#endif /* REFLOW_COMPILE_H */
