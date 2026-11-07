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

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "ir.h"
#include "ir_lua.h"
#include "value.h"

#define IR_LUA_INITIAL_FRAMES 16

typedef struct ir_lua_node_frame_t {
    const ir_node_t *next;
    size_t count;
} ir_lua_node_frame_t;

typedef struct ir_lua_value_frame_t {
    const reflow_value_item_t *item;
    const reflow_value_member_t *member;
    size_t count;
} ir_lua_value_frame_t;

static int table_size_hint(size_t count)
{
    return count > (size_t)INT_MAX ? INT_MAX : (int)count;
}

/* Lua 5.1 and 5.2 take int table indexes; reject positions beyond INT_MAX
 * instead of truncating them. */
static int checked_index(lua_State *L, size_t position)
{
    if (position > (size_t)INT_MAX) {
        return luaL_error(L, "intermediate representation is too large");
    }
    return (int)position;
}

static void push_string(lua_State *L, const char *data, size_t len)
{
    if (len == 0) {
        lua_pushliteral(L, "");
    } else {
        lua_pushlstring(L, data, len);
    }
}

static void set_string_field(lua_State *L, const char *key, const char *data,
                             size_t len)
{
    push_string(L, data, len);
    lua_setfield(L, -2, key);
}

static void set_size_field(lua_State *L, const char *key, size_t value)
{
    lua_pushinteger(L, (lua_Integer)value);
    lua_setfield(L, -2, key);
}

/* Replace the frame userdata at idx with one twice as large and return it.
 * Frames live in Lua-managed memory so that a Lua memory error during the
 * conversion leaves nothing to release. */
static void *grow_frames(lua_State *L, int idx, const void *frames,
                         size_t *capacity, size_t size)
{
    size_t next = *capacity * 2;
    void *grown = NULL;

    if (next < *capacity || next > SIZE_MAX / size) {
        (void)luaL_error(L, "intermediate representation is too deep");
        return NULL;
    }
    grown = lua_newuserdata(L, next * size);
    memcpy(grown, frames, *capacity * size);
    lua_replace(L, idx);
    *capacity = next;
    return grown;
}

/* Push the table for one value. For an array or object, also push its empty
 * items or members table on top and return 1 so the caller can fill it. */
static int push_value_shell(lua_State *L, const reflow_value_t *value)
{
    lua_createtable(L, 0, 2);
    switch (value->type) {
    case REFLOW_VALUE_UNDEFINED:
        lua_pushliteral(L, "undefined");
        lua_setfield(L, -2, "type");
        return 0;
    case REFLOW_VALUE_NULL:
        lua_pushliteral(L, "null");
        lua_setfield(L, -2, "type");
        return 0;
    case REFLOW_VALUE_BOOL:
        lua_pushliteral(L, "boolean");
        lua_setfield(L, -2, "type");
        lua_pushboolean(L, value->as.boolean);
        lua_setfield(L, -2, "value");
        return 0;
    case REFLOW_VALUE_NUMBER:
        lua_pushliteral(L, "number");
        lua_setfield(L, -2, "type");
        lua_pushnumber(L, (lua_Number)value->as.number);
        lua_setfield(L, -2, "value");
        return 0;
    case REFLOW_VALUE_STRING:
        lua_pushliteral(L, "string");
        lua_setfield(L, -2, "type");
        set_string_field(L, "value", value->as.string.data,
                         value->as.string.len);
        return 0;
    case REFLOW_VALUE_ARRAY:
        lua_pushliteral(L, "array");
        lua_setfield(L, -2, "type");
        lua_createtable(L, table_size_hint(value->as.array.count), 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "items");
        return 1;
    case REFLOW_VALUE_OBJECT:
        lua_pushliteral(L, "object");
        lua_setfield(L, -2, "type");
        lua_createtable(L, table_size_hint(value->as.object.count), 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "members");
        return 1;
    }
    return luaL_error(L, "unsupported static value type");
}

/* Push the table for a static value tree without C recursion. Each depth
 * keeps its items or members table in a work table and its cursor in a
 * frame. */
static void push_value(lua_State *L, const reflow_value_t *root)
{
    ir_lua_value_frame_t *frames = NULL;
    size_t capacity              = IR_LUA_INITIAL_FRAMES;
    size_t depth                 = 0;
    int frames_idx               = 0;
    int work_idx                 = 0;

    luaL_checkstack(L, 8, "converting a static value");
    frames =
        (ir_lua_value_frame_t *)lua_newuserdata(L, capacity * sizeof(*frames));
    frames_idx = lua_gettop(L);
    lua_newtable(L);
    work_idx = lua_gettop(L);

    if (push_value_shell(L, root)) {
        lua_rawseti(L, work_idx, 1);
        frames[0] = (ir_lua_value_frame_t){
            .item =
                root->type == REFLOW_VALUE_ARRAY ? root->as.array.head : NULL,
            .member =
                root->type == REFLOW_VALUE_OBJECT ? root->as.object.head : NULL,
        };
        depth = 1;
    }

    while (depth > 0) {
        ir_lua_value_frame_t *frame = &frames[depth - 1];
        const reflow_value_t *value = NULL;
        const char *key             = NULL;
        size_t key_len              = 0;
        int is_object               = frame->member != NULL;
        size_t index                = 0;

        if (frame->item != NULL) {
            value       = frame->item->value;
            frame->item = frame->item->next;
        } else if (frame->member != NULL) {
            value         = frame->member->value;
            key           = frame->member->key.data;
            key_len       = frame->member->key.len;
            frame->member = frame->member->next;
        } else {
            lua_pushnil(L);
            lua_rawseti(L, work_idx, checked_index(L, depth));
            depth--;
            continue;
        }
        index = ++frame->count;

        lua_rawgeti(L, work_idx, checked_index(L, depth));
        if (is_object) {
            lua_createtable(L, 0, 2);
            set_string_field(L, "key", key, key_len);
        }
        if (push_value_shell(L, value)) {
            if (depth == capacity) {
                frames = (ir_lua_value_frame_t *)grow_frames(
                    L, frames_idx, frames, &capacity, sizeof(*frames));
            }
            lua_rawseti(L, work_idx, checked_index(L, depth + 1));
            frames[depth] = (ir_lua_value_frame_t){
                .item   = value->type == REFLOW_VALUE_ARRAY ?
                              value->as.array.head :
                              NULL,
                .member = value->type == REFLOW_VALUE_OBJECT ?
                              value->as.object.head :
                              NULL,
            };
            depth++;
        }
        if (is_object) {
            lua_setfield(L, -2, "value");
        }
        lua_rawseti(L, -2, checked_index(L, index));
        lua_pop(L, 1);
    }

    lua_replace(L, frames_idx);
    lua_settop(L, frames_idx);
}

static void push_attrs(lua_State *L, const ir_attr_list_t *attrs)
{
    const ir_attr_t *attr = NULL;
    size_t index          = 0;

    lua_createtable(L, table_size_hint(attrs->count), 0);
    for (attr = attrs->head; attr != NULL; attr = attr->next) {
        lua_createtable(L, 0, 2);
        set_string_field(L, "name", attr->name.data, attr->name.len);
        set_string_field(L, "value", attr->value.data, attr->value.len);
        lua_rawseti(L, -2, checked_index(L, ++index));
    }
}

/* Push the table for one node. For an element, also push its empty children
 * table on top and return 1 so the caller can fill it. */
static int push_node_shell(lua_State *L, const ir_node_t *node)
{
    switch (node->type) {
    case IR_ELEMENT: {
        const ir_element_t *element = (const ir_element_t *)node;

        lua_createtable(L, 0, 7);
        lua_pushliteral(L, "element");
        lua_setfield(L, -2, "type");
        set_string_field(L, "tag_name", element->tag_name.data,
                         element->tag_name.len);
        set_size_field(L, "source_start", element->source_start);
        set_size_field(L, "source_end", element->source_end);
        push_attrs(L, &element->attrs);
        lua_setfield(L, -2, "attrs");
        if (element->directives != NULL) {
            lua_createtable(L, 0, 1);
            if (element->directives->data_scopes != NULL) {
                push_value(L, element->directives->data_scopes);
                lua_setfield(L, -2, "data");
            }
            lua_setfield(L, -2, "directives");
        }
        lua_createtable(L, table_size_hint(element->children.count), 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "children");
        return 1;
    }
    case IR_TEXT: {
        const ir_text_t *text = (const ir_text_t *)node;

        lua_createtable(L, 0, 2);
        lua_pushliteral(L, "text");
        lua_setfield(L, -2, "type");
        set_string_field(L, "text", text->text.data, text->text.len);
        return 0;
    }
    case IR_COMMENT: {
        const ir_comment_t *comment = (const ir_comment_t *)node;

        lua_createtable(L, 0, 2);
        lua_pushliteral(L, "comment");
        lua_setfield(L, -2, "type");
        set_string_field(L, "text", comment->text.data, comment->text.len);
        return 0;
    }
    case IR_ROOT:
    case IR_CHAIN:
    default:
        break;
    }
    return luaL_error(L, "unsupported intermediate representation node type");
}

/* ir:to_table() returns a new Lua table that mirrors the intermediate
 * representation. The tree is walked with explicit frames so that the C
 * stack does not grow with the HTML depth. */
static int to_table_lua(lua_State *L)
{
    const ir_t *ir              = ir_check(L, 1);
    ir_lua_node_frame_t *frames = NULL;
    size_t capacity             = IR_LUA_INITIAL_FRAMES;
    size_t depth                = 0;
    int result_idx              = 0;
    int frames_idx              = 0;
    int work_idx                = 0;

    if (ir->root == NULL) {
        return luaL_error(L, "intermediate representation has no root");
    }
    lua_settop(L, 1);
    luaL_checkstack(L, 12, "converting the intermediate representation");

    lua_createtable(L, 0, 3);
    result_idx = lua_gettop(L);
    set_string_field(L, "name", ir->name.data, ir->name.len);
    set_string_field(L, "html", ir->html.data, ir->html.len);

    frames =
        (ir_lua_node_frame_t *)lua_newuserdata(L, capacity * sizeof(*frames));
    frames_idx = lua_gettop(L);
    lua_newtable(L);
    work_idx = lua_gettop(L);

    lua_createtable(L, 0, 2);
    lua_pushliteral(L, "root");
    lua_setfield(L, -2, "type");
    lua_createtable(L, table_size_hint(ir->root->children.count), 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "children");
    lua_rawseti(L, work_idx, 1);
    lua_setfield(L, result_idx, "root");
    frames[0] = (ir_lua_node_frame_t){
        .next = ir->root->children.head,
    };
    depth = 1;

    while (depth > 0) {
        ir_lua_node_frame_t *frame = &frames[depth - 1];
        const ir_node_t *node      = frame->next;
        size_t index               = 0;

        if (node == NULL) {
            lua_pushnil(L);
            lua_rawseti(L, work_idx, checked_index(L, depth));
            depth--;
            continue;
        }
        frame->next = node->next;
        index       = ++frame->count;

        lua_rawgeti(L, work_idx, checked_index(L, depth));
        if (push_node_shell(L, node)) {
            const ir_element_t *element = (const ir_element_t *)node;

            if (element->children.head == NULL) {
                lua_pop(L, 1);
            } else {
                if (depth == capacity) {
                    frames = (ir_lua_node_frame_t *)grow_frames(
                        L, frames_idx, frames, &capacity, sizeof(*frames));
                }
                lua_rawseti(L, work_idx, checked_index(L, depth + 1));
                frames[depth] = (ir_lua_node_frame_t){
                    .next = element->children.head,
                };
                depth++;
            }
        }
        lua_rawseti(L, -2, checked_index(L, index));
        lua_pop(L, 1);
    }

    lua_settop(L, result_idx);
    return 1;
}

void ir_lua_init(lua_State *L)
{
    luaL_getmetatable(L, REFLOW_IR_MT);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, to_table_lua);
    lua_setfield(L, -2, "to_table");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}
