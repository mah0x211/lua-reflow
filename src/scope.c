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

#include "scope.h"

#define SCOPE_INITIAL_FRAMES 8

/* Only its address is used, as the marker of a name bound to nil. */
static char undefined_marker;

static int frame_index(lua_State *L, size_t depth)
{
    if (depth > INT_MAX) {
        return luaL_error(L, "scope is too deep");
    }
    return (int)depth;
}

void scope_init(scope_t *scope, lua_State *L, pool_t *pool, int globals_idx,
                int frames_idx)
{
    *scope = (scope_t){
        .L           = L,
        .pool        = pool,
        .globals_idx = globals_idx,
        .frames_idx  = frames_idx,
    };
}

int scope_push(scope_t *scope, scope_kind_t kind)
{
    lua_State *L = scope->L;

    if (scope->depth == scope->capacity) {
        size_t capacity =
            scope->capacity ? scope->capacity * 2 : SCOPE_INITIAL_FRAMES;
        unsigned char *kinds = NULL;

        if (capacity > scope->capacity) {
            kinds = (unsigned char *)pool_realloc(scope->pool, scope->kinds,
                                                  capacity);
        }
        if (kinds == NULL) {
            lua_pop(L, 1);
            return -1;
        }
        scope->kinds    = kinds;
        scope->capacity = capacity;
    }
    lua_rawseti(L, scope->frames_idx, frame_index(L, scope->depth + 1));
    scope->kinds[scope->depth++] = (unsigned char)kind;
    return 0;
}

int scope_push_frame_of(scope_t *scope, size_t depth)
{
    lua_rawgeti(scope->L, scope->frames_idx, frame_index(scope->L, depth));
    return scope_push(scope, SCOPE_DATA);
}

void scope_pop_to(scope_t *scope, size_t depth)
{
    lua_State *L = scope->L;

    while (scope->depth > depth) {
        lua_pushnil(L);
        lua_rawseti(L, scope->frames_idx, frame_index(L, scope->depth));
        scope->depth--;
    }
}

void scope_push_undefined(lua_State *L)
{
    lua_pushlightuserdata(L, &undefined_marker);
}

void scope_push_dollar(const scope_t *scope)
{
    lua_pushvalue(scope->L, scope->globals_idx);
}

static void push_binding(const scope_t *scope, int data_only, const char *name,
                         size_t len)
{
    lua_State *L = scope->L;
    size_t depth = scope->depth;

    lua_pushlstring(L, name, len);
    for (; depth > scope->floor; depth--) {
        if (data_only && scope->kinds[depth - 1] != SCOPE_DATA) {
            continue;
        }
        lua_rawgeti(L, scope->frames_idx, frame_index(L, depth));
        lua_pushvalue(L, -2);
        lua_rawget(L, -2);
        if (!lua_isnil(L, -1)) {
            if (lua_touserdata(L, -1) == (void *)&undefined_marker) {
                lua_pushnil(L);
                lua_replace(L, -2);
            }
            lua_replace(L, -3);
            lua_pop(L, 1);
            return;
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
    lua_pushnil(L);
}

void scope_push_at(const scope_t *scope, const char *name, size_t len)
{
    push_binding(scope, 1, name, len);
}

void scope_push_dot(const scope_t *scope, const char *name, size_t len)
{
    push_binding(scope, 0, name, len);
}
