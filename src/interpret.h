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
 * x-include hooks supplied by higher layers.
 *
 * When `hooks` is non-NULL, interpret honors x-include: it evaluates the
 * expression, calls `get_template` for the resolved name, and recurses
 * with a fresh scope environment inheriting only the globals. Include
 * depth and cycles are tracked with the hook's ceiling.
 */
typedef struct interpret_include_hooks {
    /* user-supplied context passed to get_template. */
    void *ud;
    /*
     * Return the compiled IR root registered under (name, name_len).
     * When out_html / out_html_len are non-NULL they are additionally
     * populated with the included template's source so any error raised
     * inside the include reports the correct location.
     *
     * Return NULL when the name is not registered; interpret then emits
     * a ReflowIncludeError with reason=not_found.
     */
    const ir_node *(*get_template)(void *ud,
                                   const char *name, size_t name_len,
                                   const char **out_html,
                                   size_t *out_html_len,
                                   reflow_error *err);
    /* Maximum recursion depth for x-include. */
    int max_depth;
} interpret_include_hooks;

/*
 * Render a compiled IR tree into `out`.
 *
 *   arena         : render-scope allocations (freed by caller via
 *                   arena_destroy after render).
 *   root          : IR tree returned by compile_template.
 *   globals       : $ scope; may be NULL for none.
 *   L, helpers_ref: helper registry table stored in the Lua registry via
 *                   luaL_ref; pass LUA_NOREF when no helpers are registered.
 *   template_name : the current template's name, used to annotate
 *                   runtime errors; may be NULL.
 *   html, html_len: the current template's source, used for snippet
 *                   generation on runtime errors; may be NULL / 0.
 *   hooks         : x-include support; NULL disables x-include (a template
 *                   using it will report a runtime error).
 *
 * Returns 0 on success; -1 on runtime error (err set).
 */
int interpret_render(arena_t *arena,
                     const ir_node *root,
                     reflow_value  *globals,
                     lua_State     *L,
                     int            helpers_ref,
                     const char    *template_name,
                     const char    *html,
                     size_t         html_len,
                     const interpret_include_hooks *hooks,
                     buf_t         *out,
                     reflow_error  *err);

/*
 * Render a single element into `out`.  The caller must have already
 * located `target` inside a compiled template and confirmed that no
 * ancestor requires runtime execution (no chain / match / x-for /
 * x-each / x-with / x-data on the ascent).  Under that contract the
 * target renders as if it were the sole child of a root document.
 *
 * The target's own directives (x-text, x-html, x-bind, x-for, x-each,
 * x-match, x-include) are honoured, so a multi-emission target
 * produces the concatenated output.
 */
int interpret_render_fragment(arena_t *arena,
                              const ir_node *target,
                              reflow_value  *globals,
                              lua_State     *L,
                              int            helpers_ref,
                              const char    *template_name,
                              const char    *html,
                              size_t         html_len,
                              const interpret_include_hooks *hooks,
                              buf_t         *out,
                              reflow_error  *err);

/*
 * Reason codes describing how many target emissions the guarded
 * fragment render observed.  Returned by
 * interpret_render_fragment_at when emission count is not exactly one
 * so the caller can raise the appropriate ReflowSelectorError.
 */
typedef enum {
    INTERPRET_FRAG_OK              = 0,
    INTERPRET_FRAG_NO_MATCH        = 1,
    INTERPRET_FRAG_MULTIPLE_MATCHES= 2,
    INTERPRET_FRAG_ERROR           = -1,
} interpret_fragment_result;

/*
 * Render `target` into `out` while executing the ancestor control-flow
 * (chain / match branch selection, x-for / x-each iteration,
 * x-data / x-with scope frames).  Emission count is tallied at the
 * terminal, and the function returns:
 *
 *   INTERPRET_FRAG_OK               — target emitted exactly once;
 *                                     `out` contains its rendered HTML.
 *   INTERPRET_FRAG_NO_MATCH         — no path reached the target
 *                                     (branch check failed on the way).
 *   INTERPRET_FRAG_MULTIPLE_MATCHES — more than one path reached the
 *                                     target (e.g. ancestor x-for loops).
 *   INTERPRET_FRAG_ERROR            — a runtime error fired during the
 *                                     ancestor walk; err is populated.
 */
interpret_fragment_result
interpret_render_fragment_at(arena_t *arena,
                             const ir_node *target,
                             reflow_value  *globals,
                             lua_State     *L,
                             int            helpers_ref,
                             const char    *template_name,
                             const char    *html,
                             size_t         html_len,
                             const interpret_include_hooks *hooks,
                             buf_t         *out,
                             reflow_error  *err);

/*
 * Positional fragment rendering.
 *
 * Given a set of candidates that share the same parent and carry
 * positional predicates, walk the parent's children in emission
 * order, count sibling positions, capture each candidate emission,
 * and evaluate the predicates once totals are known.
 *
 * `parent` may be NULL to indicate a walk over the root's children
 * (root-level candidates).  In that case `root_children` /
 * `n_root_children` supply the sibling list.
 *
 * `eval_fn` is invoked once per candidate emission with the sibling
 * position figures known at that point:
 *   index         — 1-based order among all emitted siblings
 *   total         — grand total emitted siblings
 *   of_type_index — 1-based order among siblings of the same tag
 *   of_type_total — grand total emitted siblings of that tag
 * `predicate_ud` is the opaque per-candidate token supplied in the
 * `candidate_ud` array (parallel to `candidates`).
 *
 * Result semantics mirror interpret_render_fragment_at.
 */
typedef bool (*interpret_fragment_positional_eval_fn)(
    void  *predicate_ud,
    size_t index, size_t total,
    size_t of_type_index, size_t of_type_total);

interpret_fragment_result
interpret_render_fragment_positional(
    arena_t *arena,
    const ir_node *parent,
    struct ir_node * const *root_children,
    size_t         n_root_children,
    const ir_node **candidates,
    void         **candidate_ud,
    size_t         n_candidates,
    interpret_fragment_positional_eval_fn eval_fn,
    reflow_value  *globals,
    lua_State     *L,
    int            helpers_ref,
    const char    *template_name,
    const char    *html,
    size_t         html_len,
    const interpret_include_hooks *hooks,
    buf_t         *out,
    reflow_error  *err);

#endif /* REFLOW_INTERPRET_H */
