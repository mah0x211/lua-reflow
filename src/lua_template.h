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
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#ifndef REFLOW_LUA_TEMPLATE_H
#define REFLOW_LUA_TEMPLATE_H

#include <lua.h>
#include "ir.h"
#include "selector/index.h"

/*
 * Lua-visible compiled template.
 *
 * A `reflow.template` userdata bundles everything needed to render a
 * template into a stand-alone value: the IR tree, the selector index,
 * the source HTML (for runtime error snippets), the directive prefix,
 * and a registry ref that keeps the compile_arena alive.  The userdata
 * is intended to be passed around like any other Lua object — stored
 * in tables, keyed on names, or handed to `template:render(...)`
 * directly.  All lifetime is bound to the userdata: when it becomes
 * unreachable, __gc releases the arena ref and the tree is collected
 * with the rest of the arena's chunks.
 */

typedef struct reflow_template {
    int              arena_ref;   /* LUA_NOREF after __gc */
    const ir_node   *root;
    const sel_index *sindex;
    const char      *html;
    size_t           html_len;
    char             prefix[16];
    size_t           prefix_len;
} reflow_template;

#define REFLOW_TEMPLATE_MT "reflow.template"

/*
 * Register the reflow.template metatable on L.  Idempotent; the first
 * call installs the metatable and __gc, subsequent calls are no-ops.
 */
void reflow_template_register(lua_State *L);

/*
 * Return the reflow_template pointer at stack index `idx`, or NULL if
 * the value is not a template userdata.  Uses luaL_testudata semantics
 * so callers can perform a type check without raising.
 */
reflow_template *reflow_template_check(lua_State *L, int idx);

/*
 * Push the module table exposing the factory and methods so callers
 * can `require('reflow.template')`.  The module table is a callable
 * factory (via __call) equivalent to `new_template(html, opts)`.
 */
int luaopen_reflow_template(lua_State *L);

#endif /* REFLOW_LUA_TEMPLATE_H */
