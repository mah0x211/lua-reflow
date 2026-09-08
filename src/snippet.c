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

#include <stdio.h>

#include <lauxlib.h>
#include <lua.h>

#include "snippet.h"

reflow_source_location_t
reflow_source_location(const char *source, size_t source_len, size_t offset)
{
    reflow_source_location_t location = {
        .line   = 1,
        .column = 1,
    };
    size_t line_start = 0;
    size_t i          = 0;

    if (source == NULL) {
        source_len = 0;
    }
    if (offset > source_len) {
        offset = source_len;
    }
    for (i = 0; i < offset; i++) {
        if (source[i] == '\n') {
            location.line++;
            line_start = i + 1;
        }
    }
    location.column = offset - line_start + 1;
    return location;
}

static size_t decimal_width(size_t value)
{
    size_t width = 1;

    while (value >= 10) {
        value /= 10;
        width++;
    }
    return width;
}

static void add_repeat(luaL_Buffer *buffer, char value, size_t count)
{
    while (count != 0) {
        luaL_addchar(buffer, value);
        count--;
    }
}

static void begin_line(luaL_Buffer *buffer, int *has_output)
{
    if (*has_output) {
        luaL_addchar(buffer, '\n');
    }
    *has_output = 1;
}

static void add_gutter(luaL_Buffer *buffer, size_t line, size_t width)
{
    char number[3 * sizeof(size_t) + 1] = {0};
    int length                          = 0;
    size_t number_len                   = 0;

    length = snprintf(number, sizeof(number), "%zu", line);
    if (length > 0) {
        number_len = (size_t)length;
    }
    if (number_len < width) {
        add_repeat(buffer, ' ', width - number_len);
    }
    luaL_addlstring(buffer, number, number_len);
    luaL_addstring(buffer, " | ");
}

void reflow_snippet_push(lua_State *L, const char *source, size_t source_len,
                         size_t start, size_t end, size_t context_lines)
{
    reflow_source_location_t start_location = {0};
    reflow_source_location_t end_location   = {0};
    luaL_Buffer buffer;
    size_t total_lines = 1;
    size_t first_line  = 1;
    size_t last_line   = 1;
    size_t gutter      = 1;
    size_t cursor      = 0;
    size_t line        = 1;
    size_t i           = 0;
    int has_output     = 0;

    if (source == NULL) {
        source     = "";
        source_len = 0;
    }
    if (start > source_len) {
        start = source_len;
    }
    if (end > source_len) {
        end = source_len;
    }
    if (end < start) {
        end = start;
    }

    start_location = reflow_source_location(source, source_len, start);
    end_location   = reflow_source_location(source, source_len, end);
    for (i = 0; i < source_len; i++) {
        if (source[i] == '\n') {
            total_lines++;
        }
    }
    first_line = start_location.line > context_lines ?
                     start_location.line - context_lines :
                     1;
    if (context_lines > total_lines - end_location.line) {
        last_line = total_lines;
    } else {
        last_line = end_location.line + context_lines;
    }
    gutter = decimal_width(last_line);

    luaL_buffinit(L, &buffer);
    while (line <= last_line) {
        size_t line_start = cursor;
        size_t line_end   = cursor;

        while (line_end < source_len && source[line_end] != '\n') {
            line_end++;
        }
        if (line >= first_line) {
            begin_line(&buffer, &has_output);
            add_gutter(&buffer, line, gutter);
            luaL_addlstring(&buffer, source + line_start,
                            line_end - line_start);

            if (line == start_location.line) {
                size_t highlight_start = start_location.column - 1;
                size_t highlight_len   = 0;

                if (start_location.line == end_location.line) {
                    highlight_len = end - start;
                } else if (line_end - line_start > highlight_start) {
                    highlight_len = line_end - line_start - highlight_start;
                }
                if (highlight_len == 0) {
                    highlight_len = 1;
                }

                begin_line(&buffer, &has_output);
                add_repeat(&buffer, ' ', gutter);
                luaL_addstring(&buffer, " | ");
                add_repeat(&buffer, ' ', highlight_start);
                add_repeat(&buffer, '^', highlight_len);
            }
        }
        cursor = line_end < source_len ? line_end + 1 : line_end;
        line++;
    }
    luaL_pushresult(&buffer);
}
