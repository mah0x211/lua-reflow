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
#include "lua_template.h"
#include "arena.h"
#include "buf.h"
#include "compile.h"
#include "compile_arena.h"
#include "error.h"
#include "interpret.h"
#include "json5.h"
#include "lua_selector.h"
#include "renderer.h"
#include "selector/cache.h"
#include "selector/parse.h"
#include "selector/resolve.h"
#include "value.h"
#include <lauxlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Metatable helpers
 * ------------------------------------------------------------------ */

reflow_template *reflow_template_check(lua_State *L, int idx)
{
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
    return (reflow_template *)luaL_testudata(L, idx, REFLOW_TEMPLATE_MT);
#else
    if (lua_type(L, idx) != LUA_TUSERDATA) return NULL;
    if (!lua_getmetatable(L, idx)) return NULL;
    luaL_getmetatable(L, REFLOW_TEMPLATE_MT);
    int eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return eq ? (reflow_template *)lua_touserdata(L, idx) : NULL;
#endif
}

static int template_gc(lua_State *L)
{
    reflow_template *t = (reflow_template *)luaL_checkudata(
        L, 1, REFLOW_TEMPLATE_MT);
    if (t->arena_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, t->arena_ref);
        t->arena_ref = LUA_NOREF;
    }
    return 0;
}

/* ------------------------------------------------------------------
 * Factory
 * ------------------------------------------------------------------ */

/* Snapshot the current top-of-stack helpers table into a name array
 * allocated inside `carena`.  `t` may be nil / absent, in which case
 * *n is set to 0 and NULL is returned. */
static const char **collect_helper_names(lua_State *L, int idx,
                                         compile_arena *carena,
                                         size_t *n)
{
    if (lua_type(L, idx) != LUA_TTABLE) {
        *n = 0;
        return NULL;
    }
    size_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        count++;
        lua_pop(L, 1);
    }
    if (count == 0) {
        *n = 0;
        return NULL;
    }
    const char **arr = (const char **)compile_arena_alloc(
        carena, L, count * sizeof(const char *));
    size_t i = 0;
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        size_t nl = 0;
        const char *nm = lua_tolstring(L, -2, &nl);
        char *dup = (char *)compile_arena_alloc(carena, L, nl + 1);
        memcpy(dup, nm, nl);
        dup[nl] = '\0';
        arr[i++] = dup;
        lua_pop(L, 1);
    }
    *n = count;
    return arr;
}

/* Push a Lua error table describing a compile error.  Copies every
 * string into Lua state so callers may destroy the compile arena
 * afterwards. */
static void push_compile_error(lua_State *L, const reflow_error *err)
{
    lua_newtable(L);
    lua_pushstring(L, err->type ? err->type : "ReflowCompileError");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, err->message ? err->message : "compile error");
    lua_setfield(L, -2, "message");
    if (err->template_name) {
        lua_pushstring(L, err->template_name);
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
    if (err->snippet) {
        lua_pushstring(L, err->snippet);
        lua_setfield(L, -2, "snippet");
    }
    if (err->element) {
        lua_pushstring(L, err->element);
        lua_setfield(L, -2, "element");
    }
    if (err->directive) {
        lua_pushstring(L, err->directive);
        lua_setfield(L, -2, "directive");
    }
    if (err->attribute) {
        lua_pushstring(L, err->attribute);
        lua_setfield(L, -2, "attribute");
    }
    if (err->expression) {
        lua_pushstring(L, err->expression);
        lua_setfield(L, -2, "expression");
    }
    luaL_getmetatable(L, "reflow.error");
    if (!lua_isnil(L, -1)) {
        lua_setmetatable(L, -2);
    } else {
        lua_pop(L, 1);
    }
}

/*
 * new_template(html, opts?) → template_ud, nil | nil, err_table
 *
 * `opts` may include `prefix` (default "x-") and `helpers` — either an
 * array of names or a set (t[name]=true).  Errors returned as the
 * standard structured error table.
 */
static int template_new(lua_State *L)
{
    size_t hlen = 0;
    const char *html = luaL_checklstring(L, 1, &hlen);

    const char *prefix     = "x-";
    size_t      prefix_len = 2;
    int         opts_idx   = 2;
    int         helpers_pos = 0;

    if (lua_type(L, opts_idx) == LUA_TTABLE) {
        lua_getfield(L, opts_idx, "prefix");
        if (lua_type(L, -1) == LUA_TSTRING) {
            prefix = lua_tolstring(L, -1, &prefix_len);
        }
        lua_pop(L, 1);
        lua_getfield(L, opts_idx, "helpers");
        if (lua_type(L, -1) == LUA_TTABLE) {
            helpers_pos = lua_gettop(L);
        } else {
            lua_pop(L, 1);
        }
    }

    if (prefix_len == 0 ||
        prefix_len >= sizeof(((reflow_template *)0)->prefix)) {
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushliteral(L, "ReflowCompileError");
        lua_setfield(L, -2, "type");
        lua_pushliteral(L, "prefix must be 1..15 bytes");
        lua_setfield(L, -2, "message");
        return 2;
    }

    compile_arena *carena = compile_arena_new(L, 4096);
    int arena_stack_pos = lua_gettop(L);

    size_t n_helpers = 0;
    const char **helpers = NULL;
    if (helpers_pos > 0) {
        /* Determine if helpers is array-form or set-form. */
        size_t alen = 0;
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 502
        alen = (size_t)lua_rawlen(L, helpers_pos);
#else
        alen = (size_t)lua_objlen(L, helpers_pos);
#endif
        if (alen > 0) {
            helpers = (const char **)compile_arena_alloc(
                carena, L, alen * sizeof(const char *));
            for (size_t i = 0; i < alen; i++) {
                lua_rawgeti(L, helpers_pos, (int)(i + 1));
                size_t nl = 0;
                const char *nm = luaL_checklstring(L, -1, &nl);
                char *dup = (char *)compile_arena_alloc(carena, L, nl + 1);
                memcpy(dup, nm, nl);
                dup[nl] = '\0';
                helpers[i] = dup;
                lua_pop(L, 1);
            }
            n_helpers = alen;
        } else {
            helpers = collect_helper_names(L, helpers_pos, carena, &n_helpers);
        }
    }

    reflow_error err = {0};
    ir_node *root = compile_template(carena, L,
                                     NULL /* template name unset */,
                                     html, hlen,
                                     prefix, prefix_len,
                                     helpers, n_helpers, &err);
    if (root == NULL) {
        lua_settop(L, arena_stack_pos);
        lua_pop(L, 1);
        lua_pushnil(L);
        push_compile_error(L, &err);
        return 2;
    }

    /* Copy html and prefix into arena so the userdata's pointers stay
     * alive with the arena. */
    char *html_copy = (char *)compile_arena_alloc(carena, L, hlen + 1);
    memcpy(html_copy, html, hlen);
    html_copy[hlen] = '\0';

    sel_index *sindex = sel_build_index(carena, L, root);
    if (sindex == NULL) {
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushliteral(L, "ReflowCompileError");
        lua_setfield(L, -2, "type");
        lua_pushliteral(L, "selector index build failed");
        lua_setfield(L, -2, "message");
        return 2;
    }

    /* Move the arena userdata from the stack into a registry ref so
     * the template userdata keeps it alive. */
    lua_pushvalue(L, arena_stack_pos);
    int arena_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_remove(L, arena_stack_pos);

    reflow_template_register(L);
    reflow_template *t = (reflow_template *)lua_newuserdata(L, sizeof(*t));
    t->arena_ref  = arena_ref;
    t->root       = root;
    t->sindex     = sindex;
    t->html       = html_copy;
    t->html_len   = hlen;
    memcpy(t->prefix, prefix, prefix_len);
    t->prefix[prefix_len] = '\0';
    t->prefix_len = prefix_len;
    luaL_getmetatable(L, REFLOW_TEMPLATE_MT);
    lua_setmetatable(L, -2);
    /* Also drop the anchor to helpers uservalue we may have introduced. */
    if (helpers_pos > 0) {
        /* helpers table stays on stack — we don't need it after
         * collect_helper_names returned. */
    }
    return 1;
}

/* ------------------------------------------------------------------
 * Include hook adapter — the templates parameter to :render is a
 * plain Lua table mapping name → template userdata.  This hook maps
 * the C-side lookup to that table.
 * ------------------------------------------------------------------ */

struct template_include_ctx {
    lua_State *L;
    int        templates_idx;   /* absolute stack index of templates table */
};

static const ir_node *template_include_lookup(void *ud,
                                              const char *name,
                                              size_t name_len,
                                              const char **out_html,
                                              size_t *out_html_len,
                                              reflow_error *err)
{
    (void)err;
    struct template_include_ctx *ctx =
        (struct template_include_ctx *)ud;
    lua_State *L = ctx->L;

    if (ctx->templates_idx == 0) return NULL;
    lua_pushlstring(L, name, name_len);
    lua_rawget(L, ctx->templates_idx);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return NULL;
    }
    reflow_template *t = reflow_template_check(L, lua_gettop(L));
    lua_pop(L, 1);
    if (t == NULL) return NULL;
    if (out_html) {
        *out_html = t->html;
        if (out_html_len) *out_html_len = t->html_len;
    }
    return t->root;
}

/* ------------------------------------------------------------------
 * Error push
 * ------------------------------------------------------------------ */

static void push_render_error(lua_State *L, const reflow_error *err,
                              const char *fallback_name)
{
    /* Reuse the renderer's push_reflow_error via its module boundary
     * if exposed; but the current renderer.c keeps it static.  Build
     * an equivalent table here so callers do not need the internal
     * helper. */
    lua_newtable(L);
    lua_pushstring(L, err->type ? err->type : "ReflowError");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, err->message ? err->message : "error");
    lua_setfield(L, -2, "message");
    const char *tn = err->template_name ? err->template_name : fallback_name;
    if (tn) {
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
    if (err->snippet)   { lua_pushstring(L, err->snippet);   lua_setfield(L, -2, "snippet"); }
    if (err->element)   { lua_pushstring(L, err->element);   lua_setfield(L, -2, "element"); }
    if (err->directive) { lua_pushstring(L, err->directive); lua_setfield(L, -2, "directive"); }
    if (err->attribute) { lua_pushstring(L, err->attribute); lua_setfield(L, -2, "attribute"); }
    if (err->expression){ lua_pushstring(L, err->expression); lua_setfield(L, -2, "expression"); }
    if (err->reason)    { lua_pushstring(L, err->reason);    lua_setfield(L, -2, "reason"); }
    if (err->requested) { lua_pushstring(L, err->requested); lua_setfield(L, -2, "requested"); }
    if (err->source)    { lua_pushstring(L, err->source);    lua_setfield(L, -2, "source"); }
    if (err->position > 0) {
        lua_pushinteger(L, (lua_Integer)err->position);
        lua_setfield(L, -2, "position");
    }
    if (err->feature)   { lua_pushstring(L, err->feature);   lua_setfield(L, -2, "feature"); }
    luaL_getmetatable(L, "reflow.error");
    if (!lua_isnil(L, -1)) {
        lua_setmetatable(L, -2);
    } else {
        lua_pop(L, 1);
    }
}

/* ------------------------------------------------------------------
 * :render(data, helpers, templates, max_depth, selector)
 *
 * All arguments are optional except `self`.  Data may be a JSON5 string
 * or nil.  Helpers is a table {name = function}.  Templates is a table
 * {name = template_userdata} used for x-include resolution.
 * Max_depth defaults to 50.  Selector may be a string (parsed on the
 * fly) or a reflow.selector userdata.
 *
 * Returns html_string, nil on success; nil, err_table on failure.
 * ------------------------------------------------------------------ */

static int template_render(lua_State *L)
{
    reflow_template *t = (reflow_template *)luaL_checkudata(
        L, 1, REFLOW_TEMPLATE_MT);
    /* Positional args: 2=data, 3=helpers, 4=templates, 5=max_depth, 6=selector */

    /* Set up render arena. */
    arena_t rarena;
    arena_init(&rarena, NULL, 0);

    /* Parse data (JSON5 string). */
    reflow_error derr = {0};
    reflow_value *globals = NULL;
    int t2 = lua_type(L, 2);
    if (t2 == LUA_TSTRING) {
        size_t dlen = 0;
        const char *ds = lua_tolstring(L, 2, &dlen);
        globals = json5_parse(ds, dlen, &rarena, &derr);
        if (derr.message != NULL) {
            arena_destroy(&rarena);
            lua_pushnil(L);
            push_render_error(L, &derr, NULL);
            return 2;
        }
    } else if (t2 != LUA_TNIL && t2 != LUA_TNONE) {
        arena_destroy(&rarena);
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushliteral(L, "ReflowRuntimeError");
        lua_setfield(L, -2, "type");
        lua_pushliteral(L, "render data must be a JSON5 string or nil");
        lua_setfield(L, -2, "message");
        return 2;
    }

    /* Helpers: ref into registry so the render can call them. */
    int helpers_ref = LUA_NOREF;
    if (lua_type(L, 3) == LUA_TTABLE) {
        lua_pushvalue(L, 3);
        helpers_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    /* Templates table for x-include lookup. */
    int templates_idx = 0;
    if (lua_type(L, 4) == LUA_TTABLE) {
        templates_idx = 4;
    }

    /* Max depth. */
    int max_depth = (int)luaL_optinteger(L, 5, 50);

    /* Selector (optional).  Accept string or reflow.selector userdata. */
    const sel_compiled *sel = NULL;
    int selector_type = lua_type(L, 6);
    if (selector_type == LUA_TSTRING) {
        /* Parse via ad-hoc compile_arena — the AST outlives this call
         * only for as long as the userdata; we do NOT cache here since
         * the template module is stateless. */
        size_t slen = 0;
        const char *ssrc = lua_tolstring(L, 6, &slen);
        compile_arena *sarena = compile_arena_new(L, 512);
        reflow_error perr = {0};
        sel_compiled *sp = selector_parse(sarena, L, ssrc, slen, &perr);
        if (sp == NULL) {
            lua_pop(L, 1);    /* arena */
            if (helpers_ref != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
            }
            arena_destroy(&rarena);
            lua_pushnil(L);
            push_render_error(L, &perr, NULL);
            return 2;
        }
        sel = sp;
        /* Keep the arena on stack so its GC anchor stays alive during
         * render; drop it on the way out. */
    } else if (selector_type == LUA_TUSERDATA) {
        reflow_selector *rs = reflow_selector_check(L, 6);
        if (rs == NULL) {
            if (helpers_ref != LUA_NOREF) {
                luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
            }
            arena_destroy(&rarena);
            lua_pushnil(L);
            lua_newtable(L);
            lua_pushliteral(L, "ReflowRuntimeError");
            lua_setfield(L, -2, "type");
            lua_pushliteral(L, "selector must be a string or reflow.selector");
            lua_setfield(L, -2, "message");
            return 2;
        }
        sel = rs->sel;
    } else if (selector_type != LUA_TNIL && selector_type != LUA_TNONE) {
        if (helpers_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        }
        arena_destroy(&rarena);
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushliteral(L, "ReflowRuntimeError");
        lua_setfield(L, -2, "type");
        lua_pushliteral(L, "selector must be a string, reflow.selector, or nil");
        lua_setfield(L, -2, "message");
        return 2;
    }

    /* Include hooks pointing at the templates table (if any). */
    struct template_include_ctx ictx = { L, templates_idx };
    interpret_include_hooks hooks = {
        .ud           = &ictx,
        .get_template = template_include_lookup,
        .max_depth    = max_depth,
    };

    /* Dispatch. */
    buf_t out;
    if (buf_init(&out) != 0) {
        if (helpers_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        }
        arena_destroy(&rarena);
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushliteral(L, "ReflowRuntimeError");
        lua_setfield(L, -2, "type");
        lua_pushliteral(L, "out of memory");
        lua_setfield(L, -2, "message");
        return 2;
    }

    reflow_error rerr = {0};
    int ok;
    if (sel != NULL) {
        /* Fragment path.  Reuse the state-based renderer's fragment
         * helper by pushing a temporary reflow_state-shaped struct
         * would be ugly; instead reimplement the search here or expose
         * a new interpret entry.  For now we call into the
         * existing render_fragment_path by staging a minimal cache. */
        /* TODO: implement fragment path.  For now, return an
         * error so callers know fragments require the state-based
         * path. */
        (void)hooks;
        buf_free(&out);
        if (helpers_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        }
        arena_destroy(&rarena);
        lua_pushnil(L);
        lua_newtable(L);
        lua_pushliteral(L, "ReflowRuntimeError");
        lua_setfield(L, -2, "type");
        lua_pushliteral(L,
            "template:render fragment path is not yet exposed via userdata");
        lua_setfield(L, -2, "message");
        return 2;
    }
    ok = interpret_render(&rarena, t->root, globals, L,
                          helpers_ref, NULL,
                          t->html, t->html_len,
                          &hooks, &out, &rerr);

    if (ok != 0) {
        if (helpers_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
        }
        push_render_error(L, &rerr, NULL);
        buf_free(&out);
        arena_destroy(&rarena);
        /* Nil first, then the error table — swap to (nil, err). */
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    lua_pushlstring(L, out.data, out.len);
    buf_free(&out);
    if (helpers_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, helpers_ref);
    }
    arena_destroy(&rarena);
    return 1;
}

/* ------------------------------------------------------------------
 * :html() / :prefix() — read-only accessors
 * ------------------------------------------------------------------ */

static int template_html(lua_State *L)
{
    reflow_template *t = (reflow_template *)luaL_checkudata(
        L, 1, REFLOW_TEMPLATE_MT);
    lua_pushlstring(L, t->html, t->html_len);
    return 1;
}

static int template_prefix(lua_State *L)
{
    reflow_template *t = (reflow_template *)luaL_checkudata(
        L, 1, REFLOW_TEMPLATE_MT);
    lua_pushlstring(L, t->prefix, t->prefix_len);
    return 1;
}

/* ------------------------------------------------------------------
 * Metatable registration
 * ------------------------------------------------------------------ */

void reflow_template_register(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_TEMPLATE_MT)) {
        lua_pushcfunction(L, template_gc);
        lua_setfield(L, -2, "__gc");

        /* __index — table of methods */
        lua_newtable(L);
        lua_pushcfunction(L, template_render);
        lua_setfield(L, -2, "render");
        lua_pushcfunction(L, template_html);
        lua_setfield(L, -2, "html");
        lua_pushcfunction(L, template_prefix);
        lua_setfield(L, -2, "prefix");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int luaopen_reflow_template(lua_State *L)
{
    reflow_template_register(L);
    lua_pushcfunction(L, template_new);
    return 1;
}
