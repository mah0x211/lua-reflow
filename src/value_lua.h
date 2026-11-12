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

#ifndef REFLOW_VALUE_LUA_H
#define REFLOW_VALUE_LUA_H

#include <lua.h>

#include "pool.h"
#include "value.h"

/* Push a JavaScript number. JavaScript has a single number type, so a
 * number with an integral value within the exact range of a double is
 * pushed as a Lua integer on Lua 5.3 and later, where tostring and
 * math.type tell the two subtypes apart; -0 becomes 0, as String(-0) is
 * "0". */
static inline void value_lua_push_number(lua_State *L, double value)
{
#if LUA_VERSION_NUM >= 503
    if (value >= -9007199254740992.0 && value <= 9007199254740992.0) {
        lua_Integer i = (lua_Integer)value;

        /* integral when truncation lost nothing, compared without == */
        if (!((double)i < value) && !((double)i > value)) {
            lua_pushinteger(L, i);
            return;
        }
    }
#endif
    lua_pushnumber(L, (lua_Number)value);
}

/* JavaScript null is the NULL light userdata, the same value as lua-cjson's
 * cjson.null. JavaScript undefined is nil. */
static inline void value_lua_push_null(lua_State *L)
{
    lua_pushlightuserdata(L, NULL);
}

static inline int value_lua_is_null(lua_State *L, int idx)
{
    return lua_type(L, idx) == LUA_TLIGHTUSERDATA &&
           lua_touserdata(L, idx) == NULL;
}

/* nil or null. */
static inline int value_lua_is_nullish(lua_State *L, int idx)
{
    return lua_isnil(L, idx) || value_lua_is_null(L, idx);
}

/**
 * Push the Lua value of a static value: arrays become sequences from index
 * 1, objects become tables with string keys, null becomes the null sentinel,
 * and undefined becomes nil. Nested values are converted without C
 * recursion; the work area is taken from pool and released before return.
 *
 * @return 0 with the value pushed, or -1 with nothing pushed when the pool
 *         runs out of memory. Lua memory errors are raised.
 */
int value_lua_push(lua_State *L, pool_t *pool, const reflow_value_t *value);

/**
 * JavaScript truthiness: nil, false, null, 0, NaN, and the empty string are
 * false; every other value, including tables and functions, is true.
 */
int value_lua_is_truthy(lua_State *L, int idx);

/**
 * Push the string JavaScript's String(n) gives for the number at idx. A Lua
 * integer is written in decimal.
 */
void value_lua_push_number_string(lua_State *L, int idx);

/* The length of the table at idx without calling metamethods. */
size_t value_lua_raw_length(lua_State *L, int idx);

/**
 * The JavaScript-style type name used in runtime error messages: "undefined"
 * for nil, "null", "boolean", "number", "string", "array" for a table whose
 * length is positive, "object" for other tables, and the Lua type name for
 * functions, userdata, and threads.
 */
const char *value_lua_type_name(lua_State *L, int idx);

#endif /* REFLOW_VALUE_LUA_H */
