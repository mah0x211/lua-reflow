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

/* Register the reflow.error metatable (idempotent). Called from
 * luaopen_reflow_compiler so tostring() on an error table works. */
void reflow_register_error_metatable(lua_State *L);

/* ============================================================
 * Fragment search for callers that already own compiled templates.
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
    /* Fragment search deliberately stops after two matches: uniqueness is
     * already disproved, and the public error mirrors the JS stopAfter=2
     * contract. Names are retained for the structured `matches` field. */
    const char   *match_template_names[2];
    size_t        match_template_name_len[2];
    lua_State    *L;
    int           helpers_idx;
    const interpret_include_hooks *hooks;
    arena_t      *rarena;
    const sel_compiled *sel;
    frag_fetch_fn_pub fetch;
    void         *fetch_ud;
    const char **stack;
    size_t      *stack_len;
    size_t       stack_capacity;
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
                       reflow_value *initial_frame,
                       reflow_error *err);

#endif /* REFLOW_RENDERER_H */
