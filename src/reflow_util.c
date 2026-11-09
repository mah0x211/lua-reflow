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

#include <stddef.h>

#include "compat.h"
#include "reflow_util.h"

int reflow_pcall_ex(lua_State *L, lua_CFunction operation, void *ctx, int nargs,
                    int nresults, reflow_cleanup_f cleanup)
{
    int function_index = 0;
    int status         = LUA_OK;

    function_index = lua_gettop(L) - nargs + 1;
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, operation, 1);
    lua_insert(L, function_index);

    status = lua_pcall(L, nargs, nresults, 0);
    cleanup(L, ctx);
    return status;
}

/* Decode one UTF-8 sequence at s[*pos] and advance *pos. Return -1 for an
 * invalid or truncated sequence. */
static long decode_utf8(const char *s, size_t len, size_t *pos)
{
    unsigned char lead = (unsigned char)s[*pos];
    size_t count       = 0;
    long code          = 0;
    size_t i           = 0;

    if (lead < 0x80) {
        (*pos)++;
        return (long)lead;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        count = 3;
        code  = lead & 0x07;
    } else if (lead >= 0xe0) {
        count = 2;
        code  = lead & 0x0f;
    } else if (lead >= 0xc2) {
        count = 1;
        code  = lead & 0x1f;
    } else {
        return -1;
    }
    if (count > len - *pos - 1) {
        return -1;
    }
    for (i = 1; i <= count; i++) {
        unsigned char next = (unsigned char)s[*pos + i];

        if ((next & 0xc0) != 0x80) {
            return -1;
        }
        code = (code << 6) | (long)(next & 0x3f);
    }
    *pos += count + 1;
    return code;
}

/* Report whether a code point is removed by JavaScript's
 * String.prototype.trim(). */
static int is_js_space(long code)
{
    return (code >= 0x09 && code <= 0x0d) || code == 0x20 || code == 0xa0 ||
           code == 0x1680 || (code >= 0x2000 && code <= 0x200a) ||
           code == 0x2028 || code == 0x2029 || code == 0x202f ||
           code == 0x205f || code == 0x3000 || code == 0xfeff;
}

/* Narrow [*start, *end) of s the way String.prototype.trim() does. */
void reflow_js_trim(const char *s, size_t *start, size_t *end)
{
    while (*start < *end) {
        size_t next = *start;

        if (!is_js_space(decode_utf8(s, *end, &next))) {
            break;
        }
        *start = next;
    }
    while (*end > *start) {
        size_t lead = *end - 1;
        size_t next = 0;

        while (lead > *start && ((unsigned char)s[lead] & 0xc0) == 0x80) {
            lead--;
        }
        next = lead;
        if (!is_js_space(decode_utf8(s, *end, &next)) || next != *end) {
            break;
        }
        *end = lead;
    }
}

/* Report whether the value is empty after String.prototype.trim(). */
int reflow_js_is_blank(const char *value, size_t len)
{
    size_t start = 0;
    size_t end   = len;

    reflow_js_trim(value, &start, &end);
    return start == end;
}
