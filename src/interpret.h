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

#ifndef REFLOW_INTERPRET_H
#define REFLOW_INTERPRET_H

#include <lua.h>
#include <stddef.h>
#include "arena.h"
#include "buf.h"
#include "error.h"
#include "ir.h"

/*
 * Render a compiled IR tree into `out`.
 *
 *   arena         : render-scope allocations (freed by caller via
 *                   arena_destroy after render).
 *   root          : IR tree returned by compile_template.
 *   globals       : $ scope; may be NULL for none.
 *   L, helpers_ref: helper registry table stored in the Lua registry via
 *                   luaL_ref; pass LUA_NOREF when no helpers are registered.
 *
 * Returns 0 on success; -1 on runtime error (err set).
 */
int interpret_render(arena_t *arena,
                     const ir_node *root,
                     reflow_value  *globals,
                     lua_State     *L,
                     int            helpers_ref,
                     buf_t         *out,
                     reflow_error  *err);

#endif /* REFLOW_INTERPRET_H */
