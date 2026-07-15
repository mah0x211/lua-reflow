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
#include "arena.h"
#include "buf.h"
#include "error.h"
#include "interpret.h"
#include "ir.h"
#include "selector/index.h"
#include "selector/parse.h"

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

/* Register the reflow.error metatable (idempotent). Called from
 * luaopen_reflow_compiler so tostring() on an error table works. */
void reflow_register_error_metatable(lua_State *L);

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

/* ============================================================
 * Fragment search — shared between the state-based path and any
 * caller that already has a compiled template (e.g. lua_template).
 * ============================================================ */

/* Include-target fetch callback used by frag_search when the outer
 * template has zero static candidates and recursion into x-include
 * targets is required.  Returns 0 on success and populates the output
 * params; -1 when the name is not registered.
 */
typedef int (*frag_fetch_fn_pub)(void *ud, lua_State *L,
                                 const char *name, size_t name_len,
                                 ir_node **out_root,
                                 const sel_index **out_sindex,
                                 const char **out_html,
                                 size_t *out_hlen);

typedef struct frag_search_pub {
    buf_t         first_match;
    size_t        match_count;
    lua_State    *L;
    int           helpers_ref;
    const interpret_include_hooks *hooks;
    arena_t      *rarena;
    const sel_compiled *sel;
    frag_fetch_fn_pub fetch;
    void         *fetch_ud;
    const char *stack[64];
    size_t      stack_len[64];
    int         depth;
    int         max_depth;
} frag_search_pub;

/* Run a fragment search over a single compiled template.  Accumulates
 * matches into ctx->first_match / ctx->match_count and, when the outer
 * template yields zero candidates, walks x-include elements via
 * ctx->fetch.  Returns 0 on success and -1 with err populated on
 * failure.
 */
int reflow_frag_search(frag_search_pub *ctx,
                       const char *tname, size_t tname_len,
                       ir_node *root, const sel_index *sindex,
                       const char *html, size_t html_len,
                       reflow_value *globals,
                       reflow_error *err);

#endif /* REFLOW_RENDERER_H */
