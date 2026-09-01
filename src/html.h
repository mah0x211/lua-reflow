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

#ifndef REFLOW_HTML_H
#define REFLOW_HTML_H

#include <lua.h>
#include <stddef.h>

/**
 * Dependency-neutral, synchronous HTML notification handler.
 *
 * A start tag is reported as on_element_begin, zero or more on_attribute
 * calls, and on_start_tag_end.  All pointers are borrowed for the duration of
 * the callback only.  Element and attribute names are ASCII-lowercase; text
 * and attribute values retain the original source bytes without character
 * reference decoding.
 *
 * Every callback is required.  A callback aborts parsing by raising a Lua
 * error.  The original error value is preserved by reflow_html_parse.
 */
typedef struct reflow_html_handler_t {
    void *ctx;

    void (*on_element_begin)(void *ctx, const char *name, size_t name_len,
                             size_t source_start, size_t source_end);
    void (*on_attribute)(void *ctx, const char *name, size_t name_len,
                         const char *value, size_t value_len);
    void (*on_start_tag_end)(void *ctx, int is_void_element);
    void (*on_element_end)(void *ctx);
    void (*on_text)(void *ctx, const char *text, size_t text_len);
    void (*on_comment)(void *ctx, const char *text, size_t text_len);
} reflow_html_handler_t;

/**
 * Parse one immutable HTML byte string and emit synchronous notifications.
 *
 * No implicit elements are synthesized.  Explicit end tags close matching
 * open elements from the inside out, unmatched end tags are ignored, and open
 * elements remaining at end of input are not closed.  Standard HTML void
 * elements close at the end of their start tag; a self-closing mark on any
 * other HTML element is not a close operation.
 *
 * @return LUA_OK on success or a lua_pcall status on failure.  On failure the
 *         original Lua error value is left on the stack top.
 */
int reflow_html_parse(lua_State *L, const char *html, size_t html_len,
                      const reflow_html_handler_t *handler);

#endif /* REFLOW_HTML_H */
