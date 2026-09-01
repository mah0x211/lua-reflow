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

#ifndef REFLOW_HTML_LEXBOR_INTERNAL_H
#define REFLOW_HTML_LEXBOR_INTERNAL_H

#include <lua.h>
#include <stddef.h>

#include <lexbor/core/base.h>
#include <lexbor/html/base.h>
#include <lexbor/html/token.h>

/**
 * Token callback invoked synchronously by the pool-backed Lexbor session.
 *
 * The token and every pointer reachable from it are borrowed only for the
 * duration of the callback.  Return the received token to let Lexbor clean and
 * reuse it for the next token, or return NULL after setting tokenizer status
 * to abort tokenization.
 */
typedef lxb_html_token_t *(*reflow_html_lexbor_token_f)(
    lxb_html_tokenizer_t *tokenizer, lxb_html_token_t *token, void *ctx);

/**
 * Tokenize one HTML input through a temporary pool-backed Lexbor session.
 *
 * token_done must be non-NULL.  html may be NULL only when html_len is zero.
 * The callback is invoked synchronously and must obey the borrowed-token
 * contract above.  The temporary tokenizer and its pool are released before
 * this function returns, including when the callback raises a Lua error.
 *
 * @return A lua_pcall status.  On LUA_OK, lexbor_status reports the tokenizer
 *         result.  On a Lua error, the original error value remains on the Lua
 *         stack and lexbor_status reports the last completed Lexbor operation.
 */
int reflow_html_lexbor_tokenize(lua_State *L, const char *html, size_t html_len,
                                reflow_html_lexbor_token_f token_done,
                                void *token_ctx, lxb_status_t *lexbor_status);

#endif /* REFLOW_HTML_LEXBOR_INTERNAL_H */
