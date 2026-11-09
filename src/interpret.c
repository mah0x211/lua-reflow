/*
 * Copyright (C) 2026 Masatoshi Fukunaga
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "compat.h"
#include "error.h"
#include "expr_eval.h"
#include "ir.h"
#include "pool.h"
#include "render_output.h"
#include "scope.h"
#include "snippet.h"
#include "value_lua.h"

#define INTERPRET_OPERATION      "reflow.interpret.render"
#define INTERPRET_INITIAL_FRAMES 16
#define INTERPRET_OOM_MESSAGE    "out of memory while rendering"

/* Stack positions inside the protected body. */
#define INTERPRET_CTX_IDX     1
#define INTERPRET_DATA_IDX    2
#define INTERPRET_HELPERS_IDX 3
#define INTERPRET_FRAMES_IDX  4

/* One open element (NULL for the root), the next child to render, and the
 * scope depth to restore when the element closes. */
typedef struct {
    const ir_element_t *element;
    const ir_node_t *next;
    size_t scope_depth;
} frame_t;

typedef struct {
    lua_State *L;
    const ir_t *ir;
    pool_t *pool;
    render_output_t out;
    frame_t *frames;
    size_t capacity;
    size_t depth;
    scope_t scope;
    expr_eval_t eval;
    /* Where a runtime error was raised, for its meta. */
    const ir_element_t *error_element;
    const reflow_expression_t *error_expression;
    const char *error_directive;
} render_ctx_t;

/* HTML void elements never have an end tag. The list is the one used by the
 * JavaScript version and by Lexbor when the template is compiled. */
static int is_void_element(const ir_string_t *tag)
{
    static const char *const names[] = {
        "area",  "base", "br",   "col",    "embed", "hr",  "img",
        "input", "link", "meta", "source", "track", "wbr", NULL,
    };
    size_t i = 0;

    for (i = 0; names[i] != NULL; i++) {
        if (strlen(names[i]) == tag->len &&
            memcmp(names[i], tag->data, tag->len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int push_frame(render_ctx_t *ctx, const ir_element_t *element,
                      const ir_node_t *next, size_t scope_depth)
{
    if (ctx->depth == ctx->capacity) {
        size_t capacity =
            ctx->capacity ? ctx->capacity * 2 : INTERPRET_INITIAL_FRAMES;
        frame_t *frames = NULL;

        if (capacity > SIZE_MAX / sizeof(*frames)) {
            errno = ENOMEM;
            return -1;
        }
        frames = (frame_t *)pool_realloc(ctx->pool, ctx->frames,
                                         capacity * sizeof(*frames));
        if (frames == NULL) {
            return -1;
        }
        ctx->frames   = frames;
        ctx->capacity = capacity;
    }
    ctx->frames[ctx->depth++] = (frame_t){
        .element     = element,
        .next        = next,
        .scope_depth = scope_depth,
    };
    return 0;
}

#define WRITE_LITERAL(out, s) render_output_write((out), (s), sizeof(s) - 1)

static int write_start_tag(render_output_t *out, const ir_element_t *element)
{
    const ir_attr_t *attr = NULL;

    if (WRITE_LITERAL(out, "<") != 0 ||
        render_output_write(out, element->tag_name.data,
                            element->tag_name.len) != 0) {
        return -1;
    }
    for (attr = element->attrs.head; attr != NULL; attr = attr->next) {
        if (WRITE_LITERAL(out, " ") != 0 ||
            render_output_write(out, attr->name.data, attr->name.len) != 0 ||
            WRITE_LITERAL(out, "=\"") != 0 ||
            render_output_write_attr_value(out, attr->value.data,
                                           attr->value.len) != 0 ||
            WRITE_LITERAL(out, "\"") != 0) {
            return -1;
        }
    }
    return WRITE_LITERAL(out, ">");
}

static int write_end_tag(render_output_t *out, const ir_element_t *element)
{
    if (WRITE_LITERAL(out, "</") != 0 ||
        render_output_write(out, element->tag_name.data,
                            element->tag_name.len) != 0) {
        return -1;
    }
    return WRITE_LITERAL(out, ">");
}

/* Write the value of an x-text expression as escaped text. nil and null
 * write nothing; tables, functions, userdata, and threads are errors. */
static int write_text_content(render_ctx_t *ctx, const ir_element_t *element,
                              const reflow_expression_t *expression)
{
    lua_State *L  = ctx->L;
    const char *s = NULL;
    size_t len    = 0;
    int rc        = 0;

    ctx->error_element    = element;
    ctx->error_expression = expression;
    if (expr_eval(&ctx->eval, expression) != 0) {
        return -1;
    }
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        break;
    case LUA_TBOOLEAN:
        rc = lua_toboolean(L, -1) ? WRITE_LITERAL(&ctx->out, "true") :
                                    WRITE_LITERAL(&ctx->out, "false");
        break;
    case LUA_TNUMBER:
        value_lua_push_number_string(L, -1);
        s  = lua_tolstring(L, -1, &len);
        rc = render_output_write_text(&ctx->out, s, len);
        lua_pop(L, 1);
        break;
    case LUA_TSTRING:
        s  = lua_tolstring(L, -1, &len);
        rc = render_output_write_text(&ctx->out, s, len);
        break;
    default:
        if (value_lua_is_null(L, -1)) {
            break;
        }
        ctx->error_directive = "x-text";
        lua_pushfstring(L, "x-text: value must be primitive, got %s",
                        value_lua_type_name(L, -1));
        return expr_eval_error(&ctx->eval);
    }
    lua_pop(L, 1);
    ctx->error_element    = NULL;
    ctx->error_expression = NULL;
    return rc;
}

/* Render an element: push its x-data frame, write the start tag and its
 * x-text content, and open a frame for its children unless it has none. */
static int render_element(render_ctx_t *ctx, const ir_element_t *element)
{
    const ir_directives_t *directives = element->directives;
    size_t scope_depth                = ctx->scope.depth;
    int is_void                       = is_void_element(&element->tag_name);

    if (directives != NULL && directives->data_scopes != NULL &&
        (value_lua_push(ctx->L, ctx->pool, directives->data_scopes) != 0 ||
         scope_push(&ctx->scope, SCOPE_DATA) != 0)) {
        return -1;
    } else if (write_start_tag(&ctx->out, element) != 0) {
        return -1;
    }
    if (directives != NULL && directives->text != NULL) {
        /* x-text replaces the children. A void element keeps no end tag, as
         * in the JavaScript version. */
        if (write_text_content(ctx, element, directives->text) != 0 ||
            (!is_void && write_end_tag(&ctx->out, element) != 0)) {
            return -1;
        }
        scope_pop_to(&ctx->scope, scope_depth);
        return 0;
    } else if (is_void) {
        scope_pop_to(&ctx->scope, scope_depth);
        return 0;
    }
    return push_frame(ctx, element, element->children.head, scope_depth);
}

/* Render one node. An element that may have children opens a new frame;
 * chain nodes render nothing until branch selection is implemented. */
static int render_node(render_ctx_t *ctx, const ir_node_t *node)
{
    switch (node->type) {
    case IR_TEXT: {
        const ir_text_t *text = (const ir_text_t *)node;
        return render_output_write(&ctx->out, text->text.data, text->text.len);
    }
    case IR_COMMENT: {
        const ir_comment_t *comment = (const ir_comment_t *)node;
        if (WRITE_LITERAL(&ctx->out, "<!--") != 0 ||
            render_output_write(&ctx->out, comment->text.data,
                                comment->text.len) != 0) {
            return -1;
        }
        return WRITE_LITERAL(&ctx->out, "-->");
    }
    case IR_ELEMENT:
        return render_element(ctx, (const ir_element_t *)node);
    case IR_ROOT:
    case IR_CHAIN:
        return 0;
    }
    return 0;
}

static int render_tree(render_ctx_t *ctx)
{
    if (push_frame(ctx, NULL, ctx->ir->root->children.head, 0) != 0) {
        return -1;
    }
    while (ctx->depth > 0) {
        frame_t *frame        = &ctx->frames[ctx->depth - 1];
        const ir_node_t *node = frame->next;

        if (node == NULL) {
            const ir_element_t *element = frame->element;
            size_t scope_depth          = frame->scope_depth;

            ctx->depth--;
            if (element != NULL && write_end_tag(&ctx->out, element) != 0) {
                return -1;
            }
            scope_pop_to(&ctx->scope, scope_depth);
            continue;
        }
        /* render_node may move the frame array, so advance first. */
        frame->next = node->next;
        if (render_node(ctx, node) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Protected body, called with the context, the data, and the helpers:
 * leaves the HTML string on success, or nothing when the pool ran out of
 * memory. Lua errors propagate to the caller's lua_pcall. */
static int render_body(lua_State *L)
{
    render_ctx_t *ctx = (render_ctx_t *)lua_touserdata(L, INTERPRET_CTX_IDX);

    lua_settop(L, INTERPRET_HELPERS_IDX);
    if (lua_isnil(L, INTERPRET_DATA_IDX)) {
        lua_newtable(L);
        lua_replace(L, INTERPRET_DATA_IDX);
    }
    lua_newtable(L);
    scope_init(&ctx->scope, L, ctx->pool, INTERPRET_DATA_IDX,
               INTERPRET_FRAMES_IDX);
    expr_eval_init(&ctx->eval, L, ctx->pool, &ctx->scope,
                   lua_isnil(L, INTERPRET_HELPERS_IDX) ? 0 :
                                                         INTERPRET_HELPERS_IDX);
    if (render_tree(ctx) != 0) {
        return 0;
    }
    lua_pushlstring(L, ctx->out.data == NULL ? "" : ctx->out.data,
                    ctx->out.len);
    return 1;
}

/* Where an error happened, for its meta. */
typedef struct {
    const char *name;
    size_t name_len;
    const ir_t *ir;
    const ir_element_t *element;
    const reflow_expression_t *expression;
    const char *directive;
} error_site_t;

static void push_error_meta(lua_State *L, const error_site_t *site)
{
    lua_createtable(L, 0, 8);
    lua_pushlstring(L, site->name, site->name_len);
    lua_setfield(L, -2, "template_name");
    lua_createtable(L, 1, 0);
    lua_pushlstring(L, site->name, site->name_len);
    lua_rawseti(L, -2, 1);
    lua_setfield(L, -2, "include_stack");
    if (site->element != NULL) {
        const ir_element_t *element       = site->element;
        reflow_source_location_t location = reflow_source_location(
            site->ir->html.data, site->ir->html.len, element->source_start);

        lua_pushinteger(L, (lua_Integer)location.line);
        lua_setfield(L, -2, "line");
        lua_pushinteger(L, (lua_Integer)location.column);
        lua_setfield(L, -2, "column");
        reflow_snippet_push(L, site->ir->html.data, site->ir->html.len,
                            element->source_start, element->source_end, 2);
        lua_setfield(L, -2, "snippet");
        lua_pushliteral(L, "<");
        lua_pushlstring(L, element->tag_name.data, element->tag_name.len);
        lua_pushliteral(L, ">");
        lua_concat(L, 3);
        lua_setfield(L, -2, "element");
    }
    if (site->expression != NULL) {
        lua_createtable(L, 0, 1);
        lua_pushlstring(L, site->expression->source.data,
                        site->expression->source.len);
        lua_setfield(L, -2, "source");
        lua_setfield(L, -2, "expression");
    }
    if (site->directive != NULL) {
        lua_pushstring(L, site->directive);
        lua_setfield(L, -2, "directive");
    }
}

/* Return nil and a runtime error. site is NULL when the template name is
 * not known; cause_idx is 0 when there is no cause. */
static int return_runtime_error(lua_State *L, const char *message,
                                const error_site_t *site, int cause_idx)
{
    int meta_idx = 0;

    if (site != NULL) {
        push_error_meta(L, site);
        meta_idx = lua_gettop(L);
    }
    reflow_error_push_runtime(L, INTERPRET_OPERATION, message, cause_idx,
                              meta_idx);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

/* Push the message of a normalized cause: its text when it is a string or a
 * number, or a generic message otherwise. */
static const char *push_cause_message(lua_State *L, int cause_idx)
{
    lua_getfield(L, cause_idx, "message");
    if (lua_type(L, -1) == LUA_TTABLE) {
        lua_getfield(L, -1, "message");
        lua_remove(L, -2);
    }
    if (lua_type(L, -1) != LUA_TSTRING && lua_type(L, -1) != LUA_TNUMBER) {
        lua_pop(L, 1);
        lua_pushliteral(L, "failed to render");
    }
    return lua_tostring(L, -1);
}

/* Return the compiled template at idx, or NULL when the value is not one. */
static const ir_t *to_ir(lua_State *L, int idx)
{
    void *ir = lua_touserdata(L, idx);

    if (ir == NULL || !lua_getmetatable(L, idx)) {
        return NULL;
    }
    luaL_getmetatable(L, REFLOW_IR_MT);
    if (!lua_rawequal(L, -1, -2)) {
        ir = NULL;
    }
    lua_pop(L, 2);
    return (const ir_t *)ir;
}

static int render_lua(lua_State *L)
{
    error_site_t site = {0};
    render_ctx_t ctx  = {0};
    int status        = LUA_OK;
    int base          = 0;

    if (lua_type(L, 1) != LUA_TSTRING ||
        (site.name = lua_tolstring(L, 1, &site.name_len), site.name_len == 0)) {
        return return_runtime_error(L, "name must be a non-empty string", NULL,
                                    0);
    }
    ctx.ir = site.ir = to_ir(L, 2);
    if (ctx.ir == NULL) {
        return return_runtime_error(L, "ir must be a compiled template", &site,
                                    0);
    } else if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TTABLE) {
        return return_runtime_error(L, "data must be a table or nil", &site, 0);
    } else if (!lua_isnoneornil(L, 4) && lua_type(L, 4) != LUA_TTABLE) {
        return return_runtime_error(L, "helpers must be a table or nil", &site,
                                    0);
    }
    lua_settop(L, 4);
    base = lua_gettop(L);

    lua_pushcfunction(L, render_body);
    ctx.pool = pool_new(L, 0);
    if (ctx.pool == NULL) {
        lua_settop(L, base);
        return return_runtime_error(L, INTERPRET_OOM_MESSAGE, &site, 0);
    }
    /* Keep the pool below the protected call so it stays reachable. */
    lua_insert(L, -2);
    ctx.L        = L;
    ctx.out.pool = ctx.pool;
    lua_pushlightuserdata(L, &ctx);
    lua_pushvalue(L, 3);
    lua_pushvalue(L, 4);
    status = lua_pcall(L, 3, 1, 0);
    pool_delete(L, ctx.pool);
    lua_remove(L, base + 1);
    if (status != LUA_OK || lua_isnil(L, -1)) {
        /* Reclaim the render's pool before building the error, so that the
         * error can be created after an out-of-memory failure on Lua 5.1 and
         * LuaJIT, which have no emergency collection. */
        lua_gc(L, LUA_GCCOLLECT, 0);
    }

    if (status == LUA_ERRRUN) {
        const char *message = NULL;
        int cause_idx       = 0;

        site.element    = ctx.error_element;
        site.expression = ctx.error_expression;
        site.directive  = ctx.error_directive;
        if (ctx.eval.raised) {
            /* The renderer raised its own message. */
            message = lua_tostring(L, base + 1);
        } else {
            /* A helper or a metamethod raised a value: keep it as the
             * cause. */
            reflow_error_to_cause(L, base + 1);
            cause_idx = base + 1;
            message   = push_cause_message(L, cause_idx);
        }
        return return_runtime_error(L, message, &site, cause_idx);
    } else if (status != LUA_OK) {
        reflow_error_to_cause(L, base + 1);
        return return_runtime_error(
            L,
            status == LUA_ERRMEM ? INTERPRET_OOM_MESSAGE : "failed to render",
            &site, base + 1);
    } else if (lua_isnil(L, -1)) {
        lua_settop(L, base);
        return return_runtime_error(L, INTERPRET_OOM_MESSAGE, &site, 0);
    }
    return 1;
}

LUALIB_API int luaopen_reflow_interpret(lua_State *L)
{
    struct luaL_Reg funcs[] = {
        {"render", render_lua},
        {NULL,     NULL      },
    };

    reflow_error_require(L, "reflow.error");
    lua_pop(L, 1);
    luaL_newlib(L, funcs);
    return 1;
}
