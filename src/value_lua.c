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

#include <lauxlib.h>
#include <lua.h>

#include "js_number.h"
#include "value_lua.h"

#define VALUE_LUA_INITIAL_FRAMES 8

/* The array or object being filled at one depth. */
typedef struct {
    const reflow_value_item_t *item;
    const reflow_value_member_t *member;
    size_t count;
} value_frame_t;

static int table_size_hint(size_t count)
{
    return count > INT_MAX ? INT_MAX : (int)count;
}

static int checked_index(lua_State *L, size_t index)
{
    if (index > INT_MAX) {
        return luaL_error(L, "static value is too large");
    }
    return (int)index;
}

/* Push a scalar value, or an empty table for an array or object. Return 1
 * when the pushed table must still be filled. */
static int push_shell(lua_State *L, const reflow_value_t *value)
{
    switch (value->type) {
    case REFLOW_VALUE_UNDEFINED:
        lua_pushnil(L);
        return 0;
    case REFLOW_VALUE_NULL:
        value_lua_push_null(L);
        return 0;
    case REFLOW_VALUE_BOOL:
        lua_pushboolean(L, value->as.boolean);
        return 0;
    case REFLOW_VALUE_NUMBER:
        lua_pushnumber(L, (lua_Number)value->as.number);
        return 0;
    case REFLOW_VALUE_STRING:
        lua_pushlstring(L, value->as.string.data, value->as.string.len);
        return 0;
    case REFLOW_VALUE_ARRAY:
        lua_createtable(L, table_size_hint(value->as.array.count), 0);
        return value->as.array.count > 0;
    case REFLOW_VALUE_OBJECT:
        lua_createtable(L, 0, table_size_hint(value->as.object.count));
        return value->as.object.count > 0;
    }
    return luaL_error(L, "unsupported static value type");
}

static value_frame_t frame_for(const reflow_value_t *value)
{
    return (value_frame_t){
        .item = value->type == REFLOW_VALUE_ARRAY ? value->as.array.head : NULL,
        .member =
            value->type == REFLOW_VALUE_OBJECT ? value->as.object.head : NULL,
    };
}

/* The containers being filled are kept in a work table indexed by depth, so
 * the Lua stack use does not grow with the nesting depth. */
int value_lua_push(lua_State *L, pool_t *pool, const reflow_value_t *value)
{
    value_frame_t *frames = NULL;
    size_t capacity       = VALUE_LUA_INITIAL_FRAMES;
    size_t depth          = 0;
    int work              = 0;

    luaL_checkstack(L, 6, "converting a static value");
    if (!push_shell(L, value)) {
        return 0;
    }
    frames = (value_frame_t *)pool_alloc(pool, capacity * sizeof(*frames));
    if (frames == NULL) {
        lua_pop(L, 1);
        return -1;
    }
    lua_newtable(L);
    work = lua_gettop(L);
    lua_pushvalue(L, work - 1);
    lua_rawseti(L, work, 1);
    frames[0] = frame_for(value);
    depth     = 1;

    while (depth > 0) {
        value_frame_t *frame                = &frames[depth - 1];
        const reflow_value_t *child         = NULL;
        const reflow_value_member_t *member = frame->member;
        size_t index                        = 0;
        size_t next_depth                   = depth;

        if (frame->item != NULL) {
            child       = frame->item->value;
            frame->item = frame->item->next;
            index       = ++frame->count;
        } else if (member != NULL) {
            child         = member->value;
            frame->member = member->next;
        } else {
            lua_pushnil(L);
            lua_rawseti(L, work, checked_index(L, depth));
            depth--;
            continue;
        }

        lua_rawgeti(L, work, checked_index(L, depth));
        if (member != NULL) {
            lua_pushlstring(L, member->key.data, member->key.len);
        }
        if (push_shell(L, child)) {
            if (depth == capacity) {
                value_frame_t *grown = NULL;

                if (capacity > SIZE_MAX / 2 / sizeof(*frames)) {
                    grown = NULL;
                } else {
                    grown = (value_frame_t *)pool_realloc(
                        pool, frames, capacity * 2 * sizeof(*frames));
                }
                if (grown == NULL) {
                    lua_settop(L, work - 2);
                    (void)pool_free(pool, frames);
                    return -1;
                }
                frames = grown;
                capacity *= 2;
            }
            lua_pushvalue(L, -1);
            lua_rawseti(L, work, checked_index(L, depth + 1));
            frames[depth] = frame_for(child);
            next_depth    = depth + 1;
        }
        if (member != NULL) {
            lua_rawset(L, -3);
        } else {
            lua_rawseti(L, -2, checked_index(L, index));
        }
        lua_pop(L, 1);
        depth = next_depth;
    }

    lua_pop(L, 1);
    (void)pool_free(pool, frames);
    return 0;
}

int value_lua_is_truthy(lua_State *L, int idx)
{
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        return 0;
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx);
    case LUA_TLIGHTUSERDATA:
        return lua_touserdata(L, idx) != NULL;
    case LUA_TNUMBER: {
        lua_Number n = lua_tonumber(L, idx);
        /* False for 0, -0, and NaN. */
        return n < 0 || n > 0;
    }
    case LUA_TSTRING: {
        size_t len = 0;
        (void)lua_tolstring(L, idx, &len);
        return len > 0;
    }
    default:
        return 1;
    }
}

void value_lua_push_number_string(lua_State *L, int idx)
{
    char buf[JS_NUMBER_BUFSIZE];
    size_t len = 0;

#if LUA_VERSION_NUM >= 503
    if (lua_isinteger(L, idx)) {
        lua_pushfstring(L, "%I", (LUAI_UACINT)lua_tointeger(L, idx));
        return;
    }
#endif
    len = js_number_format((double)lua_tonumber(L, idx), buf);
    lua_pushlstring(L, buf, len);
}

size_t value_lua_raw_length(lua_State *L, int idx)
{
#if LUA_VERSION_NUM == 501
    return lua_objlen(L, idx);
#else
    return lua_rawlen(L, idx);
#endif
}

const char *value_lua_type_name(lua_State *L, int idx)
{
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
        return "undefined";
    case LUA_TLIGHTUSERDATA:
        return lua_touserdata(L, idx) == NULL ? "null" : "userdata";
    case LUA_TTABLE:
        return value_lua_raw_length(L, idx) > 0 ? "array" : "object";
    default:
        return lua_typename(L, lua_type(L, idx));
    }
}
