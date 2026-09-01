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
#include <stddef.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "compat.h"
#include "error.h"
#include "html.h"
#include "ir.h"
#include "ir_internal.h"

#define COMPILE_OPERATION "reflow.compile.compile"

typedef struct compile_ctx_t {
    lua_State *L;
    ir_t *ir;
    ir_node_t *current_parent;
    ir_element_t *pending_element;
    const char *prefix;
    size_t prefix_len;
    int structured_failure;
} compile_ctx_t;

static void push_error_meta(lua_State *L, const char *template_name,
                            size_t template_name_len, const char *attribute,
                            size_t attribute_len)
{
    lua_createtable(L, 0, attribute == NULL ? 1 : 2);
    if (template_name != NULL) {
        lua_pushlstring(L, template_name, template_name_len);
        lua_setfield(L, -2, "template_name");
    }
    if (attribute != NULL) {
        lua_pushlstring(L, attribute, attribute_len);
        lua_setfield(L, -2, "attribute");
    }
}

static void push_compile_error(lua_State *L, const char *message,
                               const char *template_name,
                               size_t template_name_len, const char *attribute,
                               size_t attribute_len)
{
    int meta_idx = 0;

    if (template_name != NULL || attribute != NULL) {
        push_error_meta(L, template_name, template_name_len, attribute,
                        attribute_len);
        meta_idx = lua_gettop(L);
    }
    reflow_error_push_compile(L, COMPILE_OPERATION, message, 0, meta_idx);
    if (meta_idx != 0) {
        lua_remove(L, meta_idx);
    }
}

static int return_compile_error(lua_State *L, int owner_idx,
                                const char *message, const char *template_name,
                                size_t template_name_len, const char *attribute,
                                size_t attribute_len)
{
    if (owner_idx != 0) {
        lua_remove(L, owner_idx);
    }
    lua_pushnil(L);
    push_compile_error(L, message, template_name, template_name_len, attribute,
                       attribute_len);
    return 2;
}

static int return_existing_error(lua_State *L, int owner_idx)
{
    lua_remove(L, owner_idx);
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
}

static void compile_raise(compile_ctx_t *ctx, const char *message,
                          const char *attribute, size_t attribute_len)
{
    ctx->structured_failure = 1;
    push_compile_error(ctx->L, message, ctx->ir->name.data, ctx->ir->name.len,
                       attribute, attribute_len);
    (void)lua_error(ctx->L);
}

static void compile_raise_ir_error(compile_ctx_t *ctx)
{
    if (errno == ENOMEM) {
        compile_raise(
            ctx, "out of memory while building the intermediate representation",
            NULL, 0);
    } else {
        compile_raise(ctx, "failed to build the intermediate representation",
                      NULL, 0);
    }
}

static void on_element_begin(void *data, const char *name, size_t name_len,
                             size_t source_start, size_t source_end)
{
    compile_ctx_t *ctx = (compile_ctx_t *)data;

    if (ctx->pending_element != NULL) {
        compile_raise(ctx, "received an overlapping HTML start tag", NULL, 0);
    }
    ctx->pending_element =
        ir_new_element(ctx->ir, name, name_len, source_start, source_end);
    if (ctx->pending_element == NULL) {
        compile_raise_ir_error(ctx);
    }
}

static void on_attribute(void *data, const char *name, size_t name_len,
                         const char *value, size_t value_len)
{
    compile_ctx_t *ctx = (compile_ctx_t *)data;

    if (ctx->pending_element == NULL) {
        compile_raise(ctx, "received an HTML attribute without a start tag",
                      NULL, 0);
    }
    if (name_len >= ctx->prefix_len &&
        memcmp(name, ctx->prefix, ctx->prefix_len) == 0) {
        compile_raise(
            ctx, "directive attributes are not supported by this compile stage",
            name, name_len);
    }
    if (ir_append_attr(ctx->pending_element, name, name_len, value,
                       value_len) != 0) {
        compile_raise_ir_error(ctx);
    }
}

static void on_start_tag_end(void *data, int is_void_element)
{
    compile_ctx_t *ctx    = (compile_ctx_t *)data;
    ir_element_t *element = ctx->pending_element;

    if (element == NULL) {
        compile_raise(ctx, "received an incomplete HTML start tag", NULL, 0);
    }
    if (ir_append_child(ctx->current_parent, &element->node) != 0) {
        compile_raise_ir_error(ctx);
    }
    ctx->pending_element = NULL;
    if (!is_void_element) {
        ctx->current_parent = &element->node;
    }
}

static void on_element_end(void *data)
{
    compile_ctx_t *ctx = (compile_ctx_t *)data;
    ir_node_t *parent  = NULL;

    if (ctx->current_parent == NULL ||
        ctx->current_parent->type != IR_ELEMENT) {
        compile_raise(ctx, "received an unmatched HTML element end", NULL, 0);
    }
    parent = ir_node_parent(ctx->current_parent);
    if (parent == NULL) {
        compile_raise(ctx, "lost the parent of an HTML element", NULL, 0);
    }
    ctx->current_parent = parent;
}

static void on_text(void *data, const char *text, size_t text_len)
{
    compile_ctx_t *ctx = (compile_ctx_t *)data;
    ir_text_t *node    = ir_new_text(ctx->ir, text, text_len);

    if (node == NULL ||
        ir_append_child(ctx->current_parent, &node->node) != 0) {
        compile_raise_ir_error(ctx);
    }
}

static void on_comment(void *data, const char *text, size_t text_len)
{
    compile_ctx_t *ctx = (compile_ctx_t *)data;
    ir_comment_t *node = ir_new_comment(ctx->ir, text, text_len);

    if (node == NULL ||
        ir_append_child(ctx->current_parent, &node->node) != 0) {
        compile_raise_ir_error(ctx);
    }
}

static int helper_names_are_valid(lua_State *L, int idx)
{
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 2);
            return 0;
        }
        lua_pop(L, 1);
    }
    return 1;
}

static int compile_lua(lua_State *L)
{
    const char *name              = NULL;
    const char *html              = NULL;
    const char *prefix            = NULL;
    size_t name_len               = 0;
    size_t html_len               = 0;
    size_t prefix_len             = 0;
    ir_t *ir                      = NULL;
    ir_root_t *root               = NULL;
    int owner_idx                 = 0;
    int status                    = LUA_OK;
    compile_ctx_t ctx             = {0};
    reflow_html_handler_t handler = {0};

    if (lua_type(L, 1) != LUA_TSTRING) {
        return return_compile_error(L, 0, "name must be a non-empty string",
                                    NULL, 0, NULL, 0);
    }
    name = lua_tolstring(L, 1, &name_len);
    if (name_len == 0) {
        return return_compile_error(L, 0, "name must be a non-empty string",
                                    name, name_len, NULL, 0);
    }
    if (lua_type(L, 2) != LUA_TSTRING) {
        return return_compile_error(L, 0, "html must be a string", name,
                                    name_len, NULL, 0);
    }
    if (lua_type(L, 3) != LUA_TSTRING) {
        return return_compile_error(L, 0, "prefix must be a string", name,
                                    name_len, NULL, 0);
    }
    if (lua_type(L, 4) != LUA_TTABLE) {
        return return_compile_error(L, 0, "helper_names must be a table", name,
                                    name_len, NULL, 0);
    }
    if (!helper_names_are_valid(L, 4)) {
        return return_compile_error(L, 0, "helper_names keys must be strings",
                                    name, name_len, NULL, 0);
    }

    html   = lua_tolstring(L, 2, &html_len);
    prefix = lua_tolstring(L, 3, &prefix_len);
    ir     = ir_new(L);
    if (ir == NULL) {
        return return_compile_error(
            L, 0,
            "out of memory while creating the intermediate representation",
            name, name_len, NULL, 0);
    }
    owner_idx = lua_gettop(L);
    if (ir_set_source(ir, name, name_len, html, html_len) != 0) {
        return return_compile_error(
            L, owner_idx,
            errno == ENOMEM ? "out of memory while copying the compile input" :
                              "failed to copy the compile input",
            name, name_len, NULL, 0);
    }
    root = ir_new_root(ir);
    if (root == NULL) {
        return return_compile_error(
            L, owner_idx,
            errno == ENOMEM ?
                "out of memory while creating the intermediate representation "
                "root" :
                "failed to create the intermediate representation root",
            name, name_len, NULL, 0);
    }

    ctx = (compile_ctx_t){
        .L              = L,
        .ir             = ir,
        .current_parent = &root->node,
        .prefix         = prefix,
        .prefix_len     = prefix_len,
    };
    handler = (reflow_html_handler_t){
        .ctx              = &ctx,
        .on_element_begin = on_element_begin,
        .on_attribute     = on_attribute,
        .on_start_tag_end = on_start_tag_end,
        .on_element_end   = on_element_end,
        .on_text          = on_text,
        .on_comment       = on_comment,
    };
    status = reflow_html_parse(L, ir->html.data, ir->html.len, &handler);
    if (status != LUA_OK) {
        if (ctx.structured_failure) {
            return return_existing_error(L, owner_idx);
        }
        lua_pop(L, 1);
        return return_compile_error(L, owner_idx, "failed to parse HTML", name,
                                    name_len, NULL, 0);
    }
    if (ctx.pending_element != NULL) {
        return return_compile_error(L, owner_idx,
                                    "HTML parsing ended inside a start tag",
                                    name, name_len, NULL, 0);
    }

    lua_settop(L, owner_idx);
    return 1;
}

int luaopen_reflow_compile(lua_State *L)
{
    static const luaL_Reg functions[] = {
        {"compile", compile_lua},
        {NULL,      NULL       },
    };

    reflow_error_require(L, "reflow.error");
    lua_pop(L, 1);
    ir_init_metatable(L);
    luaL_newlib(L, functions);
    return 1;
}
