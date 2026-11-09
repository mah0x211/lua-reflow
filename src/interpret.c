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
#include "ir.h"
#include "pool.h"
#include "render_output.h"

#define INTERPRET_OPERATION      "reflow.interpret.render"
#define INTERPRET_INITIAL_FRAMES 16
#define INTERPRET_OOM_MESSAGE    "out of memory while rendering"

/* One open element (NULL for the root) and the next child to render. */
typedef struct {
    const ir_element_t *element;
    const ir_node_t *next;
} frame_t;

typedef struct {
    const ir_t *ir;
    pool_t *pool;
    render_output_t out;
    frame_t *frames;
    size_t capacity;
    size_t depth;
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
                      const ir_node_t *next)
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
        .element = element,
        .next    = next,
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
    case IR_ELEMENT: {
        const ir_element_t *element = (const ir_element_t *)node;
        if (write_start_tag(&ctx->out, element) != 0) {
            return -1;
        } else if (is_void_element(&element->tag_name)) {
            return 0;
        }
        return push_frame(ctx, element, element->children.head);
    }
    case IR_ROOT:
    case IR_CHAIN:
        return 0;
    }
    return 0;
}

static int render_tree(render_ctx_t *ctx)
{
    if (push_frame(ctx, NULL, ctx->ir->root->children.head) != 0) {
        return -1;
    }
    while (ctx->depth > 0) {
        frame_t *frame        = &ctx->frames[ctx->depth - 1];
        const ir_node_t *node = frame->next;

        if (node == NULL) {
            const ir_element_t *element = frame->element;

            ctx->depth--;
            if (element != NULL && write_end_tag(&ctx->out, element) != 0) {
                return -1;
            }
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

/* Protected body: leaves the HTML string on success, or nothing when the pool
 * ran out of memory. Lua memory errors propagate to the caller's lua_pcall. */
static int render_body(lua_State *L)
{
    render_ctx_t *ctx = (render_ctx_t *)lua_touserdata(L, 1);

    if (render_tree(ctx) != 0) {
        return 0;
    }
    lua_pushlstring(L, ctx->out.data == NULL ? "" : ctx->out.data,
                    ctx->out.len);
    return 1;
}

static void push_runtime_error(lua_State *L, const char *message,
                               const char *name, size_t name_len, int cause_idx)
{
    int meta_idx = 0;

    if (name != NULL) {
        lua_createtable(L, 0, 2);
        lua_pushlstring(L, name, name_len);
        lua_setfield(L, -2, "template_name");
        lua_createtable(L, 1, 0);
        lua_pushlstring(L, name, name_len);
        lua_rawseti(L, -2, 1);
        lua_setfield(L, -2, "include_stack");
        meta_idx = lua_gettop(L);
    }
    reflow_error_push_runtime(L, INTERPRET_OPERATION, message, cause_idx,
                              meta_idx);
    if (meta_idx != 0) {
        lua_remove(L, meta_idx);
    }
}

static int return_runtime_error(lua_State *L, const char *message,
                                const char *name, size_t name_len,
                                int cause_idx)
{
    push_runtime_error(L, message, name, name_len, cause_idx);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
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
    const char *name = NULL;
    size_t name_len  = 0;
    render_ctx_t ctx = {0};
    int status       = LUA_OK;
    int base         = 0;

    if (lua_type(L, 1) != LUA_TSTRING ||
        (name = lua_tolstring(L, 1, &name_len), name_len == 0)) {
        return return_runtime_error(L, "name must be a non-empty string", NULL,
                                    0, 0);
    }
    ctx.ir = to_ir(L, 2);
    if (ctx.ir == NULL) {
        return return_runtime_error(L, "ir must be a compiled template", name,
                                    name_len, 0);
    } else if (!lua_isnoneornil(L, 3) && lua_type(L, 3) != LUA_TTABLE) {
        return return_runtime_error(L, "data must be a table or nil", name,
                                    name_len, 0);
    }
    lua_settop(L, 3);
    base = lua_gettop(L);

    lua_pushcfunction(L, render_body);
    ctx.pool = pool_new(L, 0);
    if (ctx.pool == NULL) {
        lua_settop(L, base);
        return return_runtime_error(L, INTERPRET_OOM_MESSAGE, name, name_len,
                                    0);
    }
    /* Keep the pool below the protected call so it stays reachable. */
    lua_insert(L, -2);
    ctx.out.pool = ctx.pool;
    lua_pushlightuserdata(L, &ctx);
    status = lua_pcall(L, 1, 1, 0);
    pool_delete(L, ctx.pool);
    lua_remove(L, base + 1);
    if (status != LUA_OK || lua_isnil(L, -1)) {
        /* Reclaim the render's pool before building the error, so that the
         * error can be created after an out-of-memory failure on Lua 5.1 and
         * LuaJIT, which have no emergency collection. */
        lua_gc(L, LUA_GCCOLLECT, 0);
    }

    if (status != LUA_OK) {
        reflow_error_to_cause(L, base + 1);
        return return_runtime_error(
            L,
            status == LUA_ERRMEM ? INTERPRET_OOM_MESSAGE : "failed to render",
            name, name_len, base + 1);
    } else if (lua_isnil(L, -1)) {
        lua_settop(L, base);
        return return_runtime_error(L, INTERPRET_OOM_MESSAGE, name, name_len,
                                    0);
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
