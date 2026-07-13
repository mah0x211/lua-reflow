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
#ifndef REFLOW_EXPR_PARSE_H
#define REFLOW_EXPR_PARSE_H

#include <lua.h>
#include <stddef.h>
#include "../compile_arena.h"
#include "../error.h"
#include "node.h"

/*
 * Parse expression source into an AST.
 *
 * All AST memory is allocated from the arena (GC-managed via
 * lua_newuserdata chunks).  No cleanup is needed on error — the
 * arena's GC handles partially-built nodes.
 *
 * On syntax error: returns NULL, err is set.
 * On OOM: lua_newuserdata throws (longjmp to nearest pcall).
 */
expr_node *expr_parse(compile_arena *arena, lua_State *L,
                      const char *src, size_t len, reflow_error *err);

#endif
