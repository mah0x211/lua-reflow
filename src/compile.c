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

/*
 * HTML → IR compiler front end.
 *
 * This module receives SAX events from parser.c and constructs an IR forest
 * rooted at an IR_ROOT node. Text chunks are coalesced across the boundaries
 * exposed by on_text's last_in_text_node flag, so downstream code sees one
 * IR_TEXT per contiguous text run.
 *
 * At this stage every attribute — including x-* — is stored raw on the
 * element node. Directive parsing, combination validation, chain / match
 * consolidation, and orphan / break checks are layered on top by later
 * stages.
 */

// project
#include "compile.h"
#include "ir.h"
#include "parser.h"
// system
#include <stdio.h>
#include <string.h>

#define COMPILE_MAX_DEPTH 256

typedef struct {
    compile_arena *arena;
    lua_State     *L;
    const char    *html;
    size_t         html_len;
    reflow_error  *err;

    ir_node *stack[COMPILE_MAX_DEPTH];
    size_t   depth;

    /* Text coalescing buffer for the current text run. */
    char   *text_buf;
    size_t  text_len;
    size_t  text_cap;
} compile_ctx;

static ir_node *current_parent(compile_ctx *cc)
{
    return cc->stack[cc->depth - 1];
}

static void text_reserve(compile_ctx *cc, size_t need)
{
    if (cc->text_len + need <= cc->text_cap) {
        return;
    }
    size_t new_cap = cc->text_cap > 0 ? cc->text_cap : 64;
    while (new_cap < cc->text_len + need) {
        new_cap *= 2;
    }
    char *buf = (char *)compile_arena_alloc(cc->arena, cc->L, new_cap);
    if (cc->text_len > 0) {
        memcpy(buf, cc->text_buf, cc->text_len);
    }
    cc->text_buf = buf;
    cc->text_cap = new_cap;
}

static void text_append(compile_ctx *cc, const char *s, size_t n)
{
    text_reserve(cc, n);
    memcpy(cc->text_buf + cc->text_len, s, n);
    cc->text_len += n;
}

static void flush_text(compile_ctx *cc)
{
    if (cc->text_len == 0) {
        return;
    }
    char *text = (char *)compile_arena_alloc(cc->arena, cc->L,
                                             cc->text_len + 1);
    memcpy(text, cc->text_buf, cc->text_len);
    text[cc->text_len] = '\0';
    ir_node *node = ir_make_text(cc->arena, cc->L, text, cc->text_len);
    ir_add_child(cc->arena, cc->L, current_parent(cc), node);
    cc->text_len = 0;
}

static char *dup_str(compile_ctx *cc, const char *s, size_t n)
{
    if (s == NULL) {
        return NULL;
    }
    char *out = (char *)compile_arena_alloc(cc->arena, cc->L, n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static void cb_element(void *ud,
                       const char *tag_name, size_t tag_len,
                       const sax_attr *attrs, size_t n_attrs,
                       bool self_closing,
                       size_t src_start, size_t src_end,
                       int token)
{
    (void)token;
    compile_ctx *cc = (compile_ctx *)ud;
    if (cc->err != NULL && cc->err->message != NULL) {
        return;
    }

    flush_text(cc);

    if (cc->depth >= COMPILE_MAX_DEPTH) {
        if (cc->err != NULL && cc->err->message == NULL) {
            cc->err->type    = "ReflowCompileError";
            cc->err->message = "element nesting too deep";
        }
        return;
    }

    char *name = dup_str(cc, tag_name, tag_len);
    ir_node *node = ir_make_element(cc->arena, cc->L, name,
                                    src_start, src_end);

    for (size_t i = 0; i < n_attrs; i++) {
        char *aname = dup_str(cc, attrs[i].name, attrs[i].name_len);
        char *aval = (attrs[i].value != NULL)
                     ? dup_str(cc, attrs[i].value, attrs[i].value_len)
                     : NULL;
        ir_add_attr(cc->arena, cc->L, node, aname, aval);
    }

    if (self_closing) {
        /* Void / self-closed: attach directly under the current parent. */
        ir_add_child(cc->arena, cc->L, current_parent(cc), node);
    } else {
        cc->stack[cc->depth++] = node;
    }
}

static void cb_endtag(void *ud,
                      const char *tag_name, size_t tag_len,
                      int token)
{
    (void)tag_name;
    (void)tag_len;
    (void)token;
    compile_ctx *cc = (compile_ctx *)ud;
    if (cc->err != NULL && cc->err->message != NULL) {
        return;
    }

    flush_text(cc);

    /* parser.c has already verified the close tag matches the innermost
     * open element, so the stack top is the finishing node. Depth > 1
     * always holds here because parser.c would have rejected a stray close. */
    ir_node *finished = cc->stack[--cc->depth];
    ir_add_child(cc->arena, cc->L, current_parent(cc), finished);
}

static void cb_text(void *ud, const char *text, size_t len,
                    bool last_in_text_node)
{
    compile_ctx *cc = (compile_ctx *)ud;
    if (cc->err != NULL && cc->err->message != NULL) {
        return;
    }
    text_append(cc, text, len);
    if (last_in_text_node) {
        flush_text(cc);
    }
}

static void cb_comment(void *ud, const char *text, size_t len)
{
    compile_ctx *cc = (compile_ctx *)ud;
    if (cc->err != NULL && cc->err->message != NULL) {
        return;
    }
    flush_text(cc);
    char *dup = dup_str(cc, text, len);
    ir_node *node = ir_make_comment(cc->arena, cc->L, dup, len);
    ir_add_child(cc->arena, cc->L, current_parent(cc), node);
}

ir_node *compile_template(compile_arena *arena, lua_State *L,
                          const char *html, size_t html_len,
                          reflow_error *err)
{
    compile_ctx cc = {
        .arena    = arena,
        .L        = L,
        .html     = html,
        .html_len = html_len,
        .err      = err,
        .depth    = 0,
        .text_buf = NULL,
        .text_len = 0,
        .text_cap = 0,
    };

    ir_node *root = ir_make_root(arena, L);
    cc.stack[cc.depth++] = root;

    sax_handler handler = {
        .ud         = &cc,
        .on_element = cb_element,
        .on_endtag  = cb_endtag,
        .on_text    = cb_text,
        .on_comment = cb_comment,
    };

    int rc = html_parse(html, html_len, &handler, err);
    if (rc != 0 || (err != NULL && err->message != NULL)) {
        return NULL;
    }

    /* Flush any trailing text and unwind any unfinished elements. */
    flush_text(&cc);
    if (cc.depth != 1) {
        if (err != NULL && err->message == NULL) {
            err->type    = "ReflowCompileError";
            err->message = "internal: element stack not balanced";
        }
        return NULL;
    }
    return root;
}
