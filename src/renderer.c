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

/*
 * reflow instance state and lifecycle.
 *
 * Templates and helpers live in Lua-registry tables held by
 * reflow_state via luaL_ref; releasing those refs in __gc lets the
 * garbage collector reclaim every compile_arena and helper function
 * associated with the instance. There is no C-owned memory besides
 * the userdata itself.
 */

// project
#include "renderer.h"
#include "arena.h"
#include "buf.h"
#include "compile.h"
#include "compile_arena.h"
#include "error.h"
#include "expr/eval.h"
#include "expr/parse.h"
#include "interpret.h"
#include "ir.h"
#include "json5.h"
#include "selector/cache.h"
#include "selector/index.h"
#include "selector/parse.h"
#include "selector/resolve.h"
// system
#include <string.h>

#define REFLOW_STATE_MT  "reflow.state"
#define REFLOW_ERROR_MT  "reflow.error"

/*
 * Push a Lua table representation of the error onto the stack.  Every
 * string is copied into Lua state so the caller may free any arena
 * that backed the source pointers after this returns.  Combine with a
 * subsequent lua_error(L) to actually raise; the two-step form lets
 * the caller destroy render arenas between building the table and
 * raising the error.
 */
static void push_reflow_error(lua_State *L, const reflow_error *err,
                              const char *template_name)
{
    lua_newtable(L);
    lua_pushstring(L, err->type ? err->type : "ReflowError");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, err->message ? err->message : "error");
    lua_setfield(L, -2, "message");

    const char *tn = err->template_name ? err->template_name : template_name;
    if (tn != NULL) {
        lua_pushstring(L, tn);
        lua_setfield(L, -2, "templateName");
    }
    if (err->line > 0) {
        lua_pushinteger(L, (lua_Integer)err->line);
        lua_setfield(L, -2, "line");
    }
    if (err->column > 0) {
        lua_pushinteger(L, (lua_Integer)err->column);
        lua_setfield(L, -2, "column");
    }
    if (err->snippet != NULL) {
        lua_pushstring(L, err->snippet);
        lua_setfield(L, -2, "snippet");
    }
    if (err->element != NULL) {
        lua_pushstring(L, err->element);
        lua_setfield(L, -2, "element");
    }
    if (err->directive != NULL) {
        lua_pushstring(L, err->directive);
        lua_setfield(L, -2, "directive");
    }
    if (err->reason != NULL) {
        lua_pushstring(L, err->reason);
        lua_setfield(L, -2, "reason");
    }
    if (err->requested != NULL) {
        lua_pushstring(L, err->requested);
        lua_setfield(L, -2, "requested");
    }
    if (err->source != NULL) {
        lua_pushstring(L, err->source);
        lua_setfield(L, -2, "source");
    }
    if (err->position > 0) {
        lua_pushinteger(L, (lua_Integer)err->position);
        lua_setfield(L, -2, "position");
    }
    if (err->feature != NULL) {
        lua_pushstring(L, err->feature);
        lua_setfield(L, -2, "feature");
    }
    luaL_getmetatable(L, REFLOW_ERROR_MT);
    lua_setmetatable(L, -2);
}

/*
 * Push the error table and raise. Convenience wrapper when no arena
 * cleanup needs to happen between the two.
 */
static int raise_reflow_error(lua_State *L, const reflow_error *err,
                              const char *template_name)
{
    push_reflow_error(L, err, template_name);
    return lua_error(L);
}

/* __tostring for reflow.error tables: "ReflowXxxError: <message>". */
static int reflow_error_tostring(lua_State *L)
{
    lua_getfield(L, 1, "type");
    lua_getfield(L, 1, "message");
    lua_pushfstring(L, "%s: %s",
                    lua_tostring(L, -2) ? lua_tostring(L, -2) : "ReflowError",
                    lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    return 1;
}

/* Called from luaopen_reflow_compiler to install the reflow.error metatable. */
void reflow_register_error_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_ERROR_MT)) {
        lua_pushcfunction(L, reflow_error_tostring);
        lua_setfield(L, -2, "__tostring");
    }
    lua_pop(L, 1);
}

typedef struct reflow_state {
    int    templates_ref;   /* Lua table {name -> template entry} */
    int    helpers_ref;     /* Lua table {name -> function} */
    int    helper_names_ref; /* Lua table {name -> true} (compile static) */
    int    sel_cache_ref;   /* userdata; LUA_NOREF when caching disabled */
    sel_cache *sel_cache;   /* cached pointer; NULL when disabled */
    char   prefix[16];
    size_t prefix_len;
    int    max_include_depth;
    size_t selector_cache_size;
} reflow_state;

static reflow_state *check_state(lua_State *L, int idx)
{
    return (reflow_state *)luaL_checkudata(L, idx, REFLOW_STATE_MT);
}

static int state_gc(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    if (s->templates_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->templates_ref);
        s->templates_ref = LUA_NOREF;
    }
    if (s->helpers_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->helpers_ref);
        s->helpers_ref = LUA_NOREF;
    }
    if (s->helper_names_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->helper_names_ref);
        s->helper_names_ref = LUA_NOREF;
    }
    if (s->sel_cache_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->sel_cache_ref);
        s->sel_cache_ref = LUA_NOREF;
        s->sel_cache = NULL;
    }
    return 0;
}

static void ensure_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_STATE_MT)) {
        lua_pushcfunction(L, state_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);
}

int rf_state_new(lua_State *L)
{
    const char *prefix = luaL_optstring(L, 1, "x-");
    int max_include_depth = (int)luaL_optinteger(L, 2, 50);
    lua_Integer sel_cache_size = luaL_optinteger(L, 3, 128);

    size_t plen = strlen(prefix);
    if (plen == 0 || plen >= sizeof(((reflow_state *)0)->prefix)) {
        return luaL_error(L, "reflow.new: prefix must be 1..15 bytes");
    }
    if (sel_cache_size < 0) {
        return luaL_error(L,
            "reflow.new: selectorCacheSize must be a non-negative integer");
    }

    ensure_metatable(L);
    reflow_state *s = (reflow_state *)lua_newuserdata(L, sizeof(*s));
    memcpy(s->prefix, prefix, plen);
    s->prefix[plen] = '\0';
    s->prefix_len = plen;
    s->max_include_depth = max_include_depth;
    s->selector_cache_size = (size_t)sel_cache_size;
    s->templates_ref = LUA_NOREF;
    s->helpers_ref = LUA_NOREF;
    s->helper_names_ref = LUA_NOREF;
    s->sel_cache_ref = LUA_NOREF;
    s->sel_cache = NULL;

    /* templates table */
    lua_newtable(L);
    s->templates_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    /* helpers table */
    lua_newtable(L);
    s->helpers_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    /* helper names set */
    lua_newtable(L);
    s->helper_names_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    /* Selector cache — anchored on the state so its lifetime tracks
     * this instance. */
    s->sel_cache = sel_cache_new(L, (size_t)sel_cache_size);
    s->sel_cache_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, REFLOW_STATE_MT);
    lua_setmetatable(L, -2);
    return 1;
}

int rf_state_add_helper(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    const char *name = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    /* helpers[name] = function */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->helpers_ref);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);

    /* helper_names[name] = true */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->helper_names_ref);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);

    lua_settop(L, 1);
    return 1; /* return state for chaining */
}

/* Build a C string array of the currently registered helper names into
 * the compile arena. Returns count written to *n. */
static const char **helper_names_array(lua_State *L, reflow_state *s,
                                       compile_arena *carena, size_t *n)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->helper_names_ref);
    size_t cnt = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        cnt++;
        lua_pop(L, 1);
    }
    if (cnt == 0) {
        lua_pop(L, 1);
        *n = 0;
        return NULL;
    }
    const char **arr = (const char **)compile_arena_alloc(
        carena, L, cnt * sizeof(const char *));
    size_t idx = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        size_t nl = 0;
        const char *nm = lua_tolstring(L, -2, &nl);
        char *dup = (char *)compile_arena_alloc(carena, L, nl + 1);
        memcpy(dup, nm, nl);
        dup[nl] = '\0';
        arr[idx++] = dup;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    *n = idx;
    return arr;
}

int rf_state_compile(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    const char *name = luaL_checkstring(L, 2);
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 3, &hlen);

    compile_arena *carena = compile_arena_new(L, 8192);
    int carena_stack_pos = lua_gettop(L);

    /* Snapshot the current helper names for static validation. */
    size_t n_helpers = 0;
    const char **helpers = helper_names_array(L, s, carena, &n_helpers);

    reflow_error err = {0};
    ir_node *root = compile_template(carena, L,
                                     name /* template_name */,
                                     html, hlen,
                                     s->prefix, s->prefix_len,
                                     helpers, n_helpers, &err);
    if (root == NULL) {
        return raise_reflow_error(L, &err, name);
    }

    /* Copy html source into arena for render-time snippets. */
    char *html_copy = (char *)compile_arena_alloc(carena, L, hlen + 1);
    memcpy(html_copy, html, hlen);
    html_copy[hlen] = '\0';

    /* Build the selector index eagerly so fragment renders don't repeat
     * this work. The index is bump-allocated from the template's arena
     * and shares its lifetime, freed together with the compiled IR. */
    sel_index *sindex = sel_build_index(carena, L, root);
    if (sindex == NULL) {
        return luaL_error(L, "reflow.compile: selector index build failed");
    }

    /* templates[name] = { arena, root, html, index } */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    lua_createtable(L, 0, 4);
    lua_pushvalue(L, carena_stack_pos);
    lua_setfield(L, -2, "arena");
    lua_pushlightuserdata(L, root);
    lua_setfield(L, -2, "root");
    lua_pushlstring(L, html_copy, hlen);
    lua_setfield(L, -2, "html");
    lua_pushlightuserdata(L, sindex);
    lua_setfield(L, -2, "index");
    lua_setfield(L, -2, name);
    lua_pop(L, 1); /* templates */

    lua_settop(L, 1);
    return 1;
}

/* Convert a Lua value (string / table) at index `idx` into a
 * reflow_value tree in the given render arena. String is JSON5, table
 * is deep-converted. Returns NULL + sets err on failure. */
static reflow_value *value_from_lua(lua_State *L, int idx,
                                    arena_t *arena,
                                    reflow_error *err)
{
    int t = lua_type(L, idx);
    if (t == LUA_TNIL || t == LUA_TNONE) {
        return NULL;
    }
    if (t == LUA_TSTRING) {
        size_t slen = 0;
        const char *s = lua_tolstring(L, idx, &slen);
        return json5_parse(s, slen, arena, err);
    }
    if (t == LUA_TTABLE) {
        /* Round-trip through JSON isn't ideal, but the simplest correct
         * conversion given we already have json5_parse. Serialize the
         * Lua table into a JSON5 string with lua_next → then parse. For
         * now support only the string form; tables can be added later. */
        if (err) {
            err->type = "ReflowRuntimeError";
            err->message =
                "render data: pass a JSON5 string (table form not yet "
                "supported)";
        }
        return NULL;
    }
    if (err) {
        err->type = "ReflowRuntimeError";
        err->message = "render data must be nil, a string, or a table";
    }
    return NULL;
}

/* Shared context passed to include_lookup and the fragment renderer.
 * Kept at file scope so a helper function can reuse the layout. */
struct include_ctx {
    lua_State *L;
    int        templates_ref;
};

/* Look up a compiled template by name in the current reflow_state's
 * templates table. When out_html is non-NULL, the stored HTML source is
 * also handed back so interpret can rebase its source context for
 * errors raised inside the included template.  Returns NULL when the
 * entry is missing. */
static const ir_node *include_lookup(void *ud,
                                     const char *name, size_t name_len,
                                     const char **out_html,
                                     size_t *out_html_len,
                                     reflow_error *err)
{
    (void)err;
    struct include_ctx *ctx = (struct include_ctx *)ud;
    lua_State *L = ctx->L;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->templates_ref);
    lua_pushlstring(L, name, name_len);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return NULL;
    }
    lua_getfield(L, -1, "root");
    const ir_node *root = (const ir_node *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (out_html != NULL) {
        lua_getfield(L, -1, "html");
        size_t hl = 0;
        const char *hs = lua_tolstring(L, -1, &hl);
        *out_html = hs;
        if (out_html_len != NULL) *out_html_len = hl;
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    return root;
}

/* Positional predicate callback used by the positional fragment
 * renderer.  `ud` points at the sel_candidate whose positional[]
 * array we iterate; every predicate must pass for the candidate to
 * qualify as a match. */
static bool eval_positional_from_candidate(void *ud,
                                           size_t index, size_t total,
                                           size_t of_type_index,
                                           size_t of_type_total)
{
    const sel_candidate *cand = (const sel_candidate *)ud;
    for (size_t i = 0; i < cand->n_positional; i++) {
        if (!sel_eval_positional(&cand->positional[i],
                                 index, total,
                                 of_type_index, of_type_total)) {
            return false;
        }
    }
    return true;
}

/* --- x-include cross-template fragment search --- */

typedef struct {
    buf_t         first_match;
    size_t        match_count;
    reflow_state *s;
    lua_State    *L;
    const interpret_include_hooks *hooks;
    arena_t      *rarena;
    const sel_compiled *sel;
    const char *stack[64];
    size_t      stack_len[64];
    int         depth;
    int         max_depth;
} frag_search_ctx;

/* Look up a compiled template entry by name.  Returns 0 on success and
 * populates *out_root / *out_sindex / *out_html / *out_hlen.  Returns
 * -1 when the entry is missing. */
static int fetch_template_entry(lua_State *L, reflow_state *s,
                                const char *tname, size_t tname_len,
                                ir_node **out_root,
                                const sel_index **out_sindex,
                                const char **out_html, size_t *out_hlen)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    lua_pushlstring(L, tname, tname_len);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return -1;
    }
    lua_getfield(L, -1, "root");
    *out_root = (ir_node *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "index");
    *out_sindex = (const sel_index *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "html");
    *out_html = lua_tolstring(L, -1, out_hlen);
    lua_pop(L, 3);
    return 0;
}

static int frag_search(frag_search_ctx *ctx,
                       const char *tname, size_t tname_len,
                       ir_node *root, const sel_index *sindex,
                       const char *html, size_t html_len,
                       reflow_value *globals,
                       reflow_error *err);

/* Callback used by interpret_execute_at when walking to an x-include
 * element.  Evaluates the include expression under the current env,
 * performs safety checks, looks up the target template, and recurses
 * frag_search on it. */
typedef struct {
    frag_search_ctx *ctx;
    const ir_node   *include_el;
    reflow_error    *err;
} include_reach_ud;

static int include_reach_cb(void *ud, lua_State *L, reflow_error *err)
{
    include_reach_ud *rud = (include_reach_ud *)ud;
    frag_search_ctx  *ctx = rud->ctx;
    const ir_node    *el  = rud->include_el;
    const expr_node  *include_expr = el->element.directives.include_expr;
    (void)err;

    /* Evaluate include expression.  Reuse the existing helper via a
     * tiny render_ctx-based eval — we need scope + helpers.  The
     * interpret_execute_at caller already set up rc.env; we can grab
     * it from the reach_ctx, but our public callback signature does
     * not expose it.  Instead, evaluate against `env` reconstructed by
     * a helper.  For now, take the pragmatic path: use expr_eval
     * directly against the current thread's state. */
    (void)include_expr;
    (void)L;
    /* NOTE: we currently only support the trivial case where the
     * include expression is a literal string constant. */
    if (include_expr->type != EX_LITERAL ||
        include_expr->literal.tag != RV_STRING) {
        rud->err->type    = "ReflowSelectorError";
        rud->err->message = "x-include with non-literal expression is "
                            "not yet supported in fragment search";
        rud->err->reason  = "unsupported";
        rud->err->feature = "fragment:include-dynamic";
        return -1;
    }
    const char *target_name = include_expr->literal.string.data;
    size_t      target_len  = include_expr->literal.string.len;

    /* Depth and cycle checks. */
    if (ctx->depth >= ctx->max_depth) {
        rud->err->type      = "ReflowIncludeError";
        rud->err->message   = "include depth limit exceeded";
        rud->err->reason    = "depth_exceeded";
        rud->err->requested = target_name;
        return -1;
    }
    for (int i = 0; i < ctx->depth; i++) {
        if (ctx->stack_len[i] == target_len &&
            memcmp(ctx->stack[i], target_name, target_len) == 0) {
            rud->err->type      = "ReflowIncludeError";
            rud->err->message   = "include cycle detected";
            rud->err->reason    = "cycle";
            rud->err->requested = target_name;
            return -1;
        }
    }

    /* Look up the target template. */
    ir_node         *tgt_root  = NULL;
    const sel_index *tgt_sidx  = NULL;
    const char      *tgt_html  = NULL;
    size_t           tgt_hlen  = 0;
    if (fetch_template_entry(ctx->L, ctx->s, target_name, target_len,
                             &tgt_root, &tgt_sidx,
                             &tgt_html, &tgt_hlen) != 0) {
        rud->err->type      = "ReflowIncludeError";
        rud->err->message   = "template not found";
        rud->err->reason    = "not_found";
        rud->err->requested = target_name;
        return -1;
    }

    /* Push onto include stack. */
    ctx->stack[ctx->depth]     = target_name;
    ctx->stack_len[ctx->depth] = target_len;
    ctx->depth++;
    /* Recurse with the outer globals — for this stage we do not seed
     * initial data from an x-with on the include element (deferred). */
    int rc = frag_search(ctx, target_name, target_len,
                         tgt_root, tgt_sidx, tgt_html, tgt_hlen,
                         /*globals=*/NULL,
                         rud->err);
    ctx->depth--;
    return rc;
}

static int frag_search(frag_search_ctx *ctx,
                       const char *tname, size_t tname_len,
                       ir_node *root, const sel_index *sindex,
                       const char *html, size_t html_len,
                       reflow_value *globals,
                       reflow_error *err)
{
    (void)tname_len;
    if (sindex == NULL) {
        err->type    = "ReflowRuntimeError";
        err->message = "template has no selector index";
        return -1;
    }

    compile_arena *scratch = compile_arena_new(ctx->L, 1024);
    int scratch_pos = lua_gettop(ctx->L);
    sel_candidates *cands = sel_resolve(scratch, ctx->L, sindex, ctx->sel,
                                        err);
    if (cands == NULL) {
        lua_pop(ctx->L, 1);
        return -1;
    }

    if (cands->count > 0) {
        /* Positional path: group by parent. */
        if (cands->items[0].n_positional > 0) {
            const ir_node *seen_parents[32];
            size_t         n_seen = 0;
            for (size_t ci = 0; ci < cands->count; ci++) {
                const ir_node *cand_parent =
                    cands->items[ci].element->element.parent;
                bool already = false;
                for (size_t k = 0; k < n_seen; k++) {
                    if (seen_parents[k] == cand_parent) { already = true; break; }
                }
                if (already) continue;
                if (n_seen < sizeof(seen_parents)/sizeof(seen_parents[0])) {
                    seen_parents[n_seen++] = cand_parent;
                }
                const ir_node *group_els[64];
                void          *group_ud[64];
                size_t         group_n = 0;
                for (size_t j = 0; j < cands->count; j++) {
                    if (cands->items[j].element->element.parent != cand_parent) continue;
                    if (group_n >= 64) break;
                    group_els[group_n] = cands->items[j].element;
                    group_ud[group_n]  = (void *)&cands->items[j];
                    group_n++;
                }
                struct ir_node * const *rchildren = NULL;
                size_t n_rchildren = 0;
                if (cand_parent == NULL) {
                    rchildren = root->root.children;
                    n_rchildren = root->root.n_children;
                }
                buf_t group_out;
                if (buf_init(&group_out) != 0) {
                    lua_pop(ctx->L, 1);
                    err->type = "ReflowRuntimeError";
                    err->message = "out of memory";
                    return -1;
                }
                reflow_error frerr = {0};
                interpret_fragment_result fres =
                    interpret_render_fragment_positional(
                        ctx->rarena, cand_parent,
                        rchildren, n_rchildren,
                        group_els, group_ud, group_n,
                        eval_positional_from_candidate,
                        globals, ctx->L, ctx->s->helpers_ref,
                        tname, html, html_len, ctx->hooks,
                        &group_out, &frerr);
                if (fres == INTERPRET_FRAG_ERROR) {
                    lua_pop(ctx->L, 1);
                    buf_free(&group_out);
                    *err = frerr;
                    return -1;
                }
                if (fres == INTERPRET_FRAG_OK) {
                    if (ctx->match_count == 0) {
                        buf_put(&ctx->first_match, group_out.data, group_out.len);
                    }
                    ctx->match_count++;
                } else if (fres == INTERPRET_FRAG_MULTIPLE_MATCHES) {
                    ctx->match_count += 2;
                }
                buf_free(&group_out);
                if (ctx->match_count > 1) break;
            }
        } else {
            /* Non-positional path. */
            for (size_t ci = 0; ci < cands->count; ci++) {
                const ir_node *target = cands->items[ci].element;
                buf_t local;
                if (buf_init(&local) != 0) {
                    lua_pop(ctx->L, 1);
                    err->type = "ReflowRuntimeError";
                    err->message = "out of memory";
                    return -1;
                }
                reflow_error frerr = {0};
                interpret_fragment_result fres =
                    interpret_render_fragment_at(
                        ctx->rarena, target, globals, ctx->L,
                        ctx->s->helpers_ref, tname, html, html_len,
                        ctx->hooks, &local, &frerr);
                if (fres == INTERPRET_FRAG_ERROR) {
                    lua_pop(ctx->L, 1);
                    buf_free(&local);
                    *err = frerr;
                    return -1;
                }
                if (fres == INTERPRET_FRAG_OK) {
                    if (ctx->match_count == 0) {
                        buf_put(&ctx->first_match, local.data, local.len);
                    }
                    ctx->match_count++;
                } else if (fres == INTERPRET_FRAG_MULTIPLE_MATCHES) {
                    ctx->match_count += 2;
                }
                buf_free(&local);
                if (ctx->match_count > 1) break;
            }
        }
    } else if (sindex->n_includes > 0) {
        /* Cross-template fallback: walk each x-include element under
         * its ancestor scope and recurse into the target template. */
        for (size_t i = 0; i < sindex->n_includes; i++) {
            const ir_node *include_el = sindex->includes[i];
            include_reach_ud iud = {
                .ctx        = ctx,
                .include_el = include_el,
                .err        = err,
            };
            size_t reached = 0;
            int rc = interpret_execute_at(
                ctx->rarena, include_el, globals, ctx->L,
                ctx->s->helpers_ref, tname, html, html_len, ctx->hooks,
                include_reach_cb, &iud, &reached, err);
            if (rc != 0) {
                lua_pop(ctx->L, 1);
                return -1;
            }
            if (ctx->match_count > 1) break;
        }
    }

    lua_pop(ctx->L, 1);   /* scratch arena */
    (void)scratch_pos;
    return 0;
}

/*
 * Fragment render: resolve `sel_src` against the compiled index,
 * enforce the single-match contract, and render the target via
 * interpret_render_fragment. Errors follow the JS reference:
 * no_match / multiple_matches / unsupported.  The render arena is
 * always destroyed before returning.
 */
static int render_fragment_path(lua_State *L, reflow_state *s,
                                const char *name,
                                ir_node *root, const sel_index *sindex,
                                const char *html, size_t html_len,
                                reflow_value *globals,
                                const interpret_include_hooks *hooks,
                                arena_t *rarena,
                                const char *sel_src, size_t sel_len)
{
    (void)root;
    if (sindex == NULL) {
        arena_destroy(rarena);
        return luaL_error(L,
            "reflow.render: template \"%s\" has no selector index", name);
    }

    /* Parse (via cache) — the cache lives on the state so subsequent
     * calls for the same selector are constant-time. */
    reflow_error perr = {0};
    const sel_compiled *sel = sel_cache_resolve(s->sel_cache, L,
                                                sel_src, sel_len, &perr);
    if (sel == NULL) {
        arena_destroy(rarena);
        perr.template_name = name;
        push_reflow_error(L, &perr, name);
        return lua_error(L);
    }

    /* Set up the shared match state and dispatch through frag_search,
     * which handles both same-template resolution and x-include
     * cross-template fallback with a shared match count. */
    frag_search_ctx ctx = {0};
    if (buf_init(&ctx.first_match) != 0) {
        arena_destroy(rarena);
        return luaL_error(L, "out of memory");
    }
    ctx.s          = s;
    ctx.L          = L;
    ctx.hooks      = hooks;
    ctx.rarena     = rarena;
    ctx.sel        = sel;
    ctx.max_depth  = s->max_include_depth;
    ctx.stack[ctx.depth]     = name;
    ctx.stack_len[ctx.depth] = strlen(name);
    ctx.depth++;

    reflow_error serr = {0};
    int rc = frag_search(&ctx, name, strlen(name),
                         root, sindex, html, html_len,
                         globals, &serr);
    if (rc != 0) {
        buf_free(&ctx.first_match);
        if (serr.template_name == NULL) serr.template_name = name;
        push_reflow_error(L, &serr, name);
        arena_destroy(rarena);
        return lua_error(L);
    }

    if (ctx.match_count == 0) {
        buf_free(&ctx.first_match);
        reflow_error nferr = {
            .type          = "ReflowSelectorError",
            .message       = "no element matches the selector",
            .template_name = name,
            .reason        = "no_match",
            .source        = sel->source,
            .position      = 0,
        };
        push_reflow_error(L, &nferr, name);
        arena_destroy(rarena);
        return lua_error(L);
    }
    if (ctx.match_count > 1) {
        buf_free(&ctx.first_match);
        reflow_error mmerr = {
            .type          = "ReflowSelectorError",
            .message       = "multiple elements match the selector at render time",
            .template_name = name,
            .reason        = "multiple_matches",
            .source        = sel->source,
            .position      = 0,
        };
        push_reflow_error(L, &mmerr, name);
        arena_destroy(rarena);
        return lua_error(L);
    }
    lua_pushlstring(L, ctx.first_match.data, ctx.first_match.len);
    buf_free(&ctx.first_match);
    arena_destroy(rarena);
    return 1;
}
int rf_state_render(lua_State *L)
{
    reflow_state *s = check_state(L, 1);
    const char *name = luaL_checkstring(L, 2);
    /* Optional selector: string (raw) or reserved for compiled AST in
     * a future revision. When absent the whole template is rendered. */
    size_t      sel_len = 0;
    const char *sel_src = NULL;
    if (lua_type(L, 4) == LUA_TSTRING) {
        sel_src = lua_tolstring(L, 4, &sel_len);
    } else if (!lua_isnoneornil(L, 4)) {
        return luaL_error(L, "reflow.render: selector must be a string or nil");
    }

    /* Look up template. */
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        reflow_error nferr = {
            .type          = "ReflowRuntimeError",
            .message       = "template not registered",
            .template_name = name,
            .reason        = "not_found",
        };
        return raise_reflow_error(L, &nferr, name);
    }
    lua_getfield(L, -1, "root");
    ir_node *root = (ir_node *)lua_touserdata(L, -1);
    lua_pop(L, 1); /* pop root, keep entry */
    lua_getfield(L, -1, "index");
    sel_index *sindex = (sel_index *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "html");
    size_t html_len = 0;
    const char *html = lua_tolstring(L, -1, &html_len);
    lua_pop(L, 3); /* html, entry, templates */

    /* Set up render arena. */
    arena_t rarena;
    arena_init(&rarena, NULL, 0);

    reflow_error derr = {0};
    reflow_value *globals = value_from_lua(L, 3, &rarena, &derr);
    if (derr.message != NULL) {
        arena_destroy(&rarena);
        derr.template_name = name;
        return raise_reflow_error(L, &derr, name);
    }

    /* Set up include hooks. */
    struct include_ctx ictx = { L, s->templates_ref };
    interpret_include_hooks hooks = {
        .ud            = &ictx,
        .get_template  = include_lookup,
        .max_depth     = s->max_include_depth,
    };

    /* Fragment path: resolve selector, enforce single-match contract,
     * and render the target via interpret_render_fragment. */
    if (sel_src != NULL) {
        return render_fragment_path(L, s, name, root, sindex, html,
                                    html_len, globals, &hooks, &rarena,
                                    sel_src, sel_len);
    }

    /* Full-template path. */
    buf_t out;
    if (buf_init(&out) != 0) {
        arena_destroy(&rarena);
        return luaL_error(L, "out of memory");
    }
    reflow_error rerr = {0};
    int rc = interpret_render(&rarena, root, globals, L,
                              s->helpers_ref,
                              name, html, html_len,
                              &hooks, &out, &rerr);
    if (rc != 0) {
        /* Build the error table BEFORE freeing the render arena so the
         * snippet / element strings (which point into the arena) are
         * still valid when we copy them into Lua state. Only set the
         * outer template_name when interpret did not populate one — it
         * will have the included template's name if the error fired
         * inside an x-include recursion. */
        if (rerr.template_name == NULL) {
            rerr.template_name = name;
        }
        push_reflow_error(L, &rerr, name);
        buf_free(&out);
        arena_destroy(&rarena);
        return lua_error(L);
    }
    lua_pushlstring(L, out.data, out.len);
    buf_free(&out);
    arena_destroy(&rarena);
    return 1;
}

int rf_state_clear(lua_State *L)
{
    reflow_state *s = check_state(L, 1);

    lua_newtable(L);            /* result array */
    int result = lua_gettop(L);
    int n = 0;

    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    int tmpl = lua_gettop(L);

    if (lua_isnoneornil(L, 2)) {
        /* Collect every current name, then drop the whole table. */
        lua_pushnil(L);
        while (lua_next(L, tmpl)) {
            lua_pop(L, 1);              /* value */
            lua_pushvalue(L, -1);       /* dup key */
            lua_rawseti(L, result, ++n);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, s->templates_ref);
        lua_newtable(L);
        s->templates_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        const char *name = luaL_checkstring(L, 2);
        lua_getfield(L, tmpl, name);
        if (!lua_isnil(L, -1)) {
            lua_pushstring(L, name);
            lua_rawseti(L, result, ++n);
            lua_pushnil(L);
            lua_setfield(L, tmpl, name);
        }
        lua_pop(L, 1);
    }

    lua_settop(L, result);
    return 1;
}

int rf_state_templates(lua_State *L)
{
    reflow_state *s = check_state(L, 1);

    lua_newtable(L);
    int result = lua_gettop(L);
    int n = 0;

    lua_rawgeti(L, LUA_REGISTRYINDEX, s->templates_ref);
    int tmpl = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, tmpl)) {
        lua_pop(L, 1);              /* value */
        lua_pushvalue(L, -1);       /* dup key */
        lua_rawseti(L, result, ++n);
    }

    lua_settop(L, result);
    return 1;
}
