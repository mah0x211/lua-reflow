/**
 *  Copyright (C) 2026 Masatoshi Fukunaga
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef REFLOW_PARSER_H
#define REFLOW_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

/*
 * SAX-style HTML parser interface.
 *
 * Text bytes and attribute values are passed as raw pointers into the input
 * buffer with an explicit length. Entity references are NOT decoded; the
 * caller receives the source bytes verbatim. Tag names are lower-cased by
 * the underlying tokenizer.
 *
 * All pointers are valid only for the duration of the html_parse call.
 */

typedef struct {
    const char *name;
    size_t      name_len;
    const char *value;      /* pointer into input; NULL when no value */
    size_t      value_len;  /* 0 when the attribute has no value */
    size_t      source_start;
    size_t      source_end;
} sax_attr;

typedef struct sax_handler {
    void *ud;

    /* Called for each start tag. self_closing is true for void HTML elements
     * or explicit `/>`. token uniquely identifies this open element so the
     * matching on_endtag can be paired at higher layers. */
    void (*on_element)(void *ud,
                       const char *tag_name, size_t tag_len,
                       const sax_attr *attrs, size_t n_attrs,
                       bool self_closing,
                       size_t src_start, size_t src_end,
                       int token);

    /* Called for each explicit end tag. token equals the corresponding
     * on_element token (never emitted for void or self-closed elements). */
    void (*on_endtag)(void *ud,
                      const char *tag_name, size_t tag_len,
                      int token);

    /* Called for text nodes. last_in_text_node is true when this chunk is
     * the last of a contiguous text run (see JS-side coalescing). */
    void (*on_text)(void *ud,
                    const char *text, size_t len,
                    bool last_in_text_node);

    /* Called for HTML comments (text between <!-- and -->). */
    void (*on_comment)(void *ud, const char *text, size_t len);
} sax_handler;

/*
 * Tokenize `src` and dispatch SAX events to `handler`. Returns 0 on success,
 * or non-zero on error (with `err` set). Callbacks may set `err->message`
 * on their side; when set, the parser stops emitting further events.
 */
int html_parse(const char *src, size_t len,
               const sax_handler *handler,
               reflow_error *err);

#endif /* REFLOW_PARSER_H */
