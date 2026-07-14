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

#ifndef REFLOW_RENDERER_H
#define REFLOW_RENDERER_H

#include <lauxlib.h>
#include <lua.h>

/*
 * Public Lua entry points implemented in renderer.c.  They are registered
 * into the reflow.compiler module by luaopen_reflow_compiler.
 *
 * All functions receive a reflow_state userdata as their first argument.
 * The userdata carries its own __gc that releases the Lua-registry refs
 * for the internal templates and helpers tables.
 */

/* new(prefix, max_include_depth) -> state userdata */
int rf_state_new(lua_State *L);

/* add_helper(state, name, function) -> state */
int rf_state_add_helper(lua_State *L);

/* compile(state, name, html) -> state (raises on error) */
int rf_state_compile(lua_State *L);

/* render(state, name, data) -> html string; data may be nil, a string
 * (interpreted as JSON5), or a table. */
int rf_state_render(lua_State *L);

/* clear(state[, name]) -> array of names actually removed. */
int rf_state_clear(lua_State *L);

/* templates(state) -> array of names */
int rf_state_templates(lua_State *L);

#endif /* REFLOW_RENDERER_H */
