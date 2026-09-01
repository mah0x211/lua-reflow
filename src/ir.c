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
#include <stdint.h>
#include <string.h>

#include "compat.h"
#include "ir.h"
#include "ir_internal.h"
#include "pool.h"

typedef struct ir_new_ctx_t {
    ir_t *ir;
} ir_new_ctx_t;

enum {
    IR_USERVALUE_POOL  = 1,
    IR_USERVALUE_COUNT = IR_USERVALUE_POOL
};

static int ir_gc_lua(lua_State *L)
{
    ir_owner_internal_t *owner = (ir_owner_internal_t *)lua_touserdata(L, 1);

    if (owner != NULL && owner->pool != NULL) {
        pool_delete(L, owner->pool);
        owner->pool        = NULL;
        owner->public.root = NULL;
        owner->public.name = (ir_string_t){0};
        owner->public.html = (ir_string_t){0};
    }
    return 0;
}

void ir_init_metatable(lua_State *L)
{
    if (luaL_newmetatable(L, REFLOW_IR_MT)) {
        lua_pushcfunction(L, ir_gc_lua);
        lua_setfield(L, -2, "__gc");
        lua_pushstring(L, REFLOW_IR_MT);
        lua_setfield(L, -2, "__name");
        lua_pushstring(L, REFLOW_IR_MT);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

static int ir_new_protected(lua_State *L)
{
    ir_new_ctx_t *ctx          = (ir_new_ctx_t *)lua_touserdata(L, 1);
    ir_owner_internal_t *owner = NULL;
    ir_t *ir                   = NULL;
    pool_t *pool               = NULL;
    int owner_idx              = 0;
    int uservalue_idx          = 0;

    lua_settop(L, 0);

    owner     = (ir_owner_internal_t *)lua_newuserdata(L, sizeof(*owner));
    *owner    = (ir_owner_internal_t){0};
    ir        = &owner->public;
    owner_idx = lua_gettop(L);
    luaL_getmetatable(L, REFLOW_IR_MT);
    lua_setmetatable(L, owner_idx);

    lua_createtable(L, IR_USERVALUE_COUNT, 0);
    uservalue_idx = lua_gettop(L);
    lua_pushvalue(L, uservalue_idx);
    reflow_setuservalue(L, owner_idx);

    pool = pool_new(L, 0);
    if (pool == NULL) {
        return 0;
    }

    /* The array slot was reserved before pool_new.  Connect the Lua strong
     * reference before publishing the C pointer to the owner's finalizer. */
    lua_pushvalue(L, -1);
    lua_rawseti(L, uservalue_idx, IR_USERVALUE_POOL);
    owner->pool = pool;
    ctx->ir     = ir;

    lua_settop(L, owner_idx);
    return 1;
}

ir_t *ir_new(lua_State *L)
{
    ir_new_ctx_t ctx = {0};
    int base         = 0;

    if (L == NULL) {
        errno = EINVAL;
        return NULL;
    }
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, ir_new_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 1, 0)) {
    case LUA_OK:
        if (ctx.ir == NULL) {
            lua_settop(L, base);
            errno = errno == EINVAL ? EINVAL : ENOMEM;
            return NULL;
        }
        return ctx.ir;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        lua_error(L);
        return NULL;
    }
}

static pool_t *ir_pool(ir_t *ir)
{
    ir_owner_internal_t *owner = (ir_owner_internal_t *)ir;

    if (owner == NULL || owner->pool == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return owner->pool;
}

static int copy_string(ir_t *ir, const char *data, size_t len,
                       ir_string_t *result)
{
    pool_t *pool = ir_pool(ir);
    char *copy   = NULL;

    if (pool == NULL || result == NULL || (data == NULL && len != 0)) {
        errno = EINVAL;
        return -1;
    } else if (len == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }

    copy = (char *)pool_alloc(pool, len + 1);
    if (copy == NULL) {
        return -1;
    }
    if (len != 0) {
        memcpy(copy, data, len);
    }
    copy[len] = '\0';
    *result   = (ir_string_t){
        .data = copy,
        .len  = len,
    };
    return 0;
}

static void release_string(ir_t *ir, ir_string_t *value)
{
    int errnum = errno;

    if (ir_pool(ir) != NULL && value != NULL && value->data != NULL) {
        pool_free(ir_pool(ir), (void *)value->data);
        *value = (ir_string_t){0};
    }
    errno = errnum;
}

static void init_node(ir_node_t *node, ir_node_private_t *private,
                      ir_node_type_t type, ir_t *owner)
{
    *node = (ir_node_t){
        .type = type,
    };
    *private = (ir_node_private_t){
        .owner = owner,
    };
}

static ir_node_private_t *node_private(ir_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    switch (node->type) {
    case IR_ROOT:
        return &((ir_root_internal_t *)node)->private;
    case IR_ELEMENT:
        return &((ir_element_internal_t *)node)->private;
    case IR_TEXT:
        return &((ir_text_internal_t *)node)->private;
    case IR_COMMENT:
        return &((ir_comment_internal_t *)node)->private;
    default:
        return NULL;
    }
}

ir_node_t *ir_node_parent(const ir_node_t *node)
{
    ir_node_private_t *private = node_private((ir_node_t *)node);

    if (private == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return private->parent;
}

int ir_set_source(ir_t *ir, const char *name, size_t name_len, const char *html,
                  size_t html_len)
{
    ir_string_t name_copy = {0};
    ir_string_t html_copy = {0};

    if (ir_pool(ir) == NULL || ir->name.data != NULL || ir->html.data != NULL) {
        errno = EINVAL;
        return -1;
    } else if (copy_string(ir, name, name_len, &name_copy) != 0) {
        return -1;
    } else if (copy_string(ir, html, html_len, &html_copy) != 0) {
        release_string(ir, &name_copy);
        return -1;
    }
    ir->name = name_copy;
    ir->html = html_copy;
    return 0;
}

ir_root_t *ir_new_root(ir_t *ir)
{
    pool_t *pool                 = ir_pool(ir);
    ir_root_internal_t *internal = NULL;
    ir_root_t *root              = NULL;

    if (pool == NULL || ir->root != NULL) {
        errno = EINVAL;
        return NULL;
    }
    internal = (ir_root_internal_t *)pool_calloc(pool, 1, sizeof(*internal));
    if (internal == NULL) {
        return NULL;
    }
    root = &internal->public;
    init_node(&root->node, &internal->private, IR_ROOT, ir);
    internal->private.linked = 1;
    ir->root                 = root;
    return root;
}

ir_element_t *ir_new_element(ir_t *ir, const char *tag_name,
                             size_t tag_name_len, size_t source_start,
                             size_t source_end)
{
    pool_t *pool                    = ir_pool(ir);
    ir_string_t tag                 = {0};
    ir_element_internal_t *internal = NULL;
    ir_element_t *element           = NULL;

    if (pool == NULL || source_start > source_end) {
        errno = EINVAL;
        return NULL;
    } else if (copy_string(ir, tag_name, tag_name_len, &tag) != 0) {
        return NULL;
    }
    internal = (ir_element_internal_t *)pool_calloc(pool, 1, sizeof(*internal));
    if (internal == NULL) {
        release_string(ir, &tag);
        return NULL;
    }
    element = &internal->public;
    init_node(&element->node, &internal->private, IR_ELEMENT, ir);
    element->tag_name     = tag;
    element->source_start = source_start;
    element->source_end   = source_end;
    return element;
}

ir_text_t *ir_new_text(ir_t *ir, const char *text, size_t text_len)
{
    pool_t *pool                 = ir_pool(ir);
    ir_string_t copy             = {0};
    ir_text_internal_t *internal = NULL;
    ir_text_t *node              = NULL;

    if (pool == NULL || copy_string(ir, text, text_len, &copy) != 0) {
        return NULL;
    }
    internal = (ir_text_internal_t *)pool_calloc(pool, 1, sizeof(*internal));
    if (internal == NULL) {
        release_string(ir, &copy);
        return NULL;
    }
    node = &internal->public;
    init_node(&node->node, &internal->private, IR_TEXT, ir);
    node->text = copy;
    return node;
}

ir_comment_t *ir_new_comment(ir_t *ir, const char *text, size_t text_len)
{
    pool_t *pool                    = ir_pool(ir);
    ir_string_t copy                = {0};
    ir_comment_internal_t *internal = NULL;
    ir_comment_t *node              = NULL;

    if (pool == NULL || copy_string(ir, text, text_len, &copy) != 0) {
        return NULL;
    }
    internal = (ir_comment_internal_t *)pool_calloc(pool, 1, sizeof(*internal));
    if (internal == NULL) {
        release_string(ir, &copy);
        return NULL;
    }
    node = &internal->public;
    init_node(&node->node, &internal->private, IR_COMMENT, ir);
    node->text = copy;
    return node;
}

static ir_node_list_t *child_list(ir_node_t *parent)
{
    if (parent == NULL) {
        return NULL;
    } else if (parent->type == IR_ROOT) {
        return &((ir_root_t *)parent)->children;
    } else if (parent->type == IR_ELEMENT) {
        return &((ir_element_t *)parent)->children;
    }
    return NULL;
}

int ir_append_child(ir_node_t *parent, ir_node_t *child)
{
    ir_node_list_t *list           = child_list(parent);
    ir_node_private_t *parent_priv = node_private(parent);
    ir_node_private_t *child_priv  = node_private(child);

    if (list == NULL || parent_priv == NULL || child_priv == NULL ||
        parent_priv->owner == NULL || parent_priv->owner != child_priv->owner ||
        child->type == IR_ROOT || child_priv->linked || parent == child) {
        errno = EINVAL;
        return -1;
    } else if (list->count == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    }

    child->next        = NULL;
    child_priv->parent = parent;
    child_priv->linked = 1;
    if (list->tail == NULL) {
        list->head = child;
    } else {
        list->tail->next = child;
    }
    list->tail = child;
    list->count++;
    return 0;
}

int ir_append_attr(ir_element_t *element, const char *name, size_t name_len,
                   const char *value, size_t value_len)
{
    ir_t *ir               = NULL;
    pool_t *pool           = NULL;
    ir_string_t name_copy  = {0};
    ir_string_t value_copy = {0};
    ir_attr_t *attr        = NULL;
    ir_attr_list_t *list   = NULL;

    if (element == NULL || element->node.type != IR_ELEMENT ||
        node_private(&element->node) == NULL) {
        errno = EINVAL;
        return -1;
    }
    ir   = node_private(&element->node)->owner;
    pool = ir_pool(ir);
    list = &element->attrs;
    if (pool == NULL) {
        return -1;
    } else if (list->count == SIZE_MAX) {
        errno = ENOMEM;
        return -1;
    } else if (copy_string(ir, name, name_len, &name_copy) != 0) {
        return -1;
    } else if (copy_string(ir, value, value_len, &value_copy) != 0) {
        release_string(ir, &name_copy);
        return -1;
    }
    attr = (ir_attr_t *)pool_calloc(pool, 1, sizeof(*attr));
    if (attr == NULL) {
        release_string(ir, &value_copy);
        release_string(ir, &name_copy);
        return -1;
    }
    attr->name  = name_copy;
    attr->value = value_copy;
    if (list->tail == NULL) {
        list->head = attr;
    } else {
        list->tail->next = attr;
    }
    list->tail = attr;
    list->count++;
    return 0;
}
