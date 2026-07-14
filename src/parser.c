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
 * HTML SAX parser built on top of the lexbor tokenizer (no tree builder).
 *
 * Design (docs/design/03-html-parser.md §2.5):
 *   - Text bytes use token->begin/end (raw input; entities NOT decoded).
 *   - Attribute values use attr->value_begin/value_end (raw; unquoted).
 *   - Comments use token->text_start/text_end (content between <!-- and -->).
 *   - LXB_TAG__EM_DOCTYPE / _END_OF_FILE / _DOCUMENT tokens are dropped.
 *   - Explicit close tags emit on_endtag; void elements never do.
 *
 * Open/close pairing is tracked here with a small stack so that on_endtag
 * receives the same numeric token that on_element received for its match;
 * higher layers can use this to associate source ranges without walking a
 * separate tag stack of their own.
 */

// project
#include "parser.h"
// depend
#include "lexbor/html/html.h"
#include "lexbor/html/tokenizer.h"
#include "lexbor/tag/const.h"
#include "lexbor/tag/tag.h"
// system
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PARSER_MAX_ATTRS  64
#define PARSER_MAX_DEPTH  256

/* Static buffer for parse error messages (single-threaded Lua, no leak). */
static char g_parser_err[256];

/* HTML5 void element tag ids. These never receive on_endtag. */
static bool is_void_id(lxb_tag_id_t id)
{
    switch (id) {
    case LXB_TAG_AREA:  case LXB_TAG_BASE: case LXB_TAG_BR:
    case LXB_TAG_COL:   case LXB_TAG_EMBED: case LXB_TAG_HR:
    case LXB_TAG_IMG:   case LXB_TAG_INPUT: case LXB_TAG_LINK:
    case LXB_TAG_META:  case LXB_TAG_SOURCE: case LXB_TAG_TRACK:
    case LXB_TAG_WBR:
        return true;
    default:
        return false;
    }
}

typedef struct {
    int         token;
    const char *tag_name;
    size_t      tag_len;
} open_elem;

typedef struct {
    const sax_handler *h;
    reflow_error      *err;
    const char        *src_base;
    size_t             src_len;
    int                next_token;
    size_t             depth;
    open_elem          stack[PARSER_MAX_DEPTH];
} parser_state;

static void parser_error(parser_state *ps, const char *fmt, ...)
{
    if (ps->err == NULL || ps->err->message != NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_parser_err, sizeof(g_parser_err), fmt, ap);
    va_end(ap);
    ps->err->type    = "ReflowCompileError";
    ps->err->message = g_parser_err;
}

static size_t offset_of(parser_state *ps, const lxb_char_t *p)
{
    if (p == NULL) {
        return 0;
    }
    return (size_t)((const char *)p - ps->src_base);
}

/*
 * Collect attributes into a fixed-size scratch buffer.
 * Returns the number of attributes, or (size_t)-1 when the count exceeds
 * PARSER_MAX_ATTRS (an error is recorded in ps->err).
 */
static size_t collect_attrs(parser_state *ps, lxb_html_token_t *token,
                            sax_attr *out)
{
    size_t n = 0;
    for (lxb_html_token_attr_t *a = token->attr_first;
         a != NULL; a = a->next) {
        if (n >= PARSER_MAX_ATTRS) {
            parser_error(ps, "too many attributes on a single element "
                         "(max %d)", PARSER_MAX_ATTRS);
            return (size_t)-1;
        }
        out[n].name     = (const char *)a->name_begin;
        out[n].name_len = (a->name_end > a->name_begin)
                          ? (size_t)(a->name_end - a->name_begin) : 0;

        bool has_value = (a->value_begin != NULL && a->value_end != NULL &&
                          a->value_end >= a->value_begin);
        out[n].value     = has_value ? (const char *)a->value_begin : NULL;
        out[n].value_len = has_value
                           ? (size_t)(a->value_end - a->value_begin) : 0;
        out[n].source_start = offset_of(ps, a->name_begin);
        out[n].source_end   = has_value
                              ? offset_of(ps, a->value_end)
                              : offset_of(ps, a->name_end);
        n++;
    }
    return n;
}

static lxb_html_token_t *on_token_cb(lxb_html_tokenizer_t *tkz,
                                     lxb_html_token_t *token, void *ctx)
{
    (void)tkz;
    parser_state *ps = (parser_state *)ctx;

    /* Stop dispatching once an error has been reported. */
    if (ps->err != NULL && ps->err->message != NULL) {
        return token;
    }

    size_t tag_name_len = 0;
    const lxb_char_t *tag_name = lxb_tag_name_by_id(token->tag_id,
                                                    &tag_name_len);
    if (tag_name == NULL) {
        return token;
    }

    /* Dispatch by lexbor meta tag id (name-based check is unsafe because
     * lexbor uses both "#..." and "!..." prefixes for meta tags). */
    switch (token->tag_id) {
    case LXB_TAG__TEXT:
        if (ps->h->on_text != NULL) {
            /* Raw bytes: entities NOT decoded (03-html-parser §2.5). */
            ps->h->on_text(ps->h->ud,
                           (const char *)token->begin,
                           (size_t)(token->end - token->begin),
                           true);
        }
        return token;

    case LXB_TAG__EM_COMMENT:
        if (ps->h->on_comment != NULL) {
            ps->h->on_comment(ps->h->ud,
                              (const char *)token->text_start,
                              (size_t)(token->text_end -
                                       token->text_start));
        }
        return token;

    case LXB_TAG__EM_DOCTYPE:
    case LXB_TAG__END_OF_FILE:
    case LXB_TAG__DOCUMENT:
    case LXB_TAG__UNDEF:
        return token;

    default:
        break;
    }

    /* Compute source offsets: expose the outer `<...>` span rather than
     * lexbor's tag-name-only slice, so upstream snippet renderers see the
     * exact HTML the user wrote. lexbor's token->begin points just past
     * the leading `<` (or `</`), so shift left by 1 or 2 depending on
     * open/close. lexbor's token->end sits at the end of the tag NAME
     * (before any attribute space or `>`), so scan forward to find the
     * closing `>` and take one past it. */
    bool is_close = (token->type & LXB_HTML_TOKEN_TYPE_CLOSE) != 0;
    size_t bracket_prefix = is_close ? 2 : 1;
    size_t start_off = offset_of(ps, token->begin);
    if (start_off >= bracket_prefix) {
        start_off -= bracket_prefix;
    } else {
        start_off = 0;
    }
    size_t end_off = offset_of(ps, token->end);
    while (end_off < ps->src_len && ps->src_base[end_off] != '>') {
        end_off++;
    }
    if (end_off < ps->src_len) {
        end_off += 1; /* one past `>` */
    }

    /* Explicit close tag. */
    if (is_close) {
        if (ps->depth == 0) {
            parser_error(ps, "unexpected close tag </%.*s> "
                         "at offset %zu",
                         (int)tag_name_len, (const char *)tag_name,
                         start_off);
            return token;
        }
        open_elem top = ps->stack[ps->depth - 1];
        if (top.tag_len != tag_name_len ||
            memcmp(top.tag_name, tag_name, tag_name_len) != 0) {
            parser_error(ps, "mismatched close tag </%.*s> "
                         "(expected </%.*s>) at offset %zu",
                         (int)tag_name_len, (const char *)tag_name,
                         (int)top.tag_len, top.tag_name,
                         start_off);
            return token;
        }
        ps->depth--;
        if (ps->h->on_endtag != NULL) {
            ps->h->on_endtag(ps->h->ud,
                             (const char *)tag_name, tag_name_len,
                             top.token);
        }
        return token;
    }

    /* Open tag. */
    sax_attr attrs[PARSER_MAX_ATTRS];
    size_t n_attrs = collect_attrs(ps, token, attrs);
    if (n_attrs == (size_t)-1) {
        return token;
    }

    bool self_closing = is_void_id(token->tag_id) ||
                        (token->type & LXB_HTML_TOKEN_TYPE_CLOSE_SELF);

    int tok_id = ps->next_token++;

    if (!self_closing) {
        if (ps->depth >= PARSER_MAX_DEPTH) {
            parser_error(ps, "element nesting too deep (max %d)",
                         PARSER_MAX_DEPTH);
            return token;
        }
        ps->stack[ps->depth].token    = tok_id;
        ps->stack[ps->depth].tag_name = (const char *)tag_name;
        ps->stack[ps->depth].tag_len  = tag_name_len;
        ps->depth++;
    }

    if (ps->h->on_element != NULL) {
        ps->h->on_element(ps->h->ud,
                          (const char *)tag_name, tag_name_len,
                          attrs, n_attrs,
                          self_closing,
                          start_off, end_off,
                          tok_id);
    }
    return token;
}

int html_parse(const char *src, size_t len,
               const sax_handler *handler, reflow_error *err)
{
    if (handler == NULL) {
        return -1;
    }

    parser_state ps = {
        .h          = handler,
        .err        = err,
        .src_base   = src,
        .src_len    = len,
        .next_token = 0,
        .depth      = 0,
    };

    lxb_html_tokenizer_t *tkz = lxb_html_tokenizer_create();
    if (tkz == NULL) {
        parser_error(&ps, "failed to allocate HTML tokenizer");
        return -1;
    }

    lxb_status_t s = lxb_html_tokenizer_init(tkz);
    if (s != LXB_STATUS_OK) {
        lxb_html_tokenizer_destroy(tkz);
        parser_error(&ps, "failed to initialize HTML tokenizer");
        return -1;
    }

    /* Custom tag table so #text, #comment, #doctype, etc. reach us. */
    s = lxb_html_tokenizer_tags_make(tkz, 128);
    if (s != LXB_STATUS_OK) {
        lxb_html_tokenizer_destroy(tkz);
        parser_error(&ps, "failed to create HTML tag table");
        return -1;
    }

    lxb_html_tokenizer_callback_token_done_set(tkz, on_token_cb, &ps);

    s = lxb_html_tokenizer_begin(tkz);
    if (s != LXB_STATUS_OK) {
        lxb_html_tokenizer_tags_destroy(tkz);
        lxb_html_tokenizer_destroy(tkz);
        parser_error(&ps, "failed to begin HTML tokenization");
        return -1;
    }

    s = lxb_html_tokenizer_chunk(tkz, (const lxb_char_t *)src, len);
    if (s != LXB_STATUS_OK && (err == NULL || err->message == NULL)) {
        parser_error(&ps, "HTML tokenizer chunk failed (status %d)",
                     (int)s);
    }

    lxb_html_tokenizer_end(tkz);
    lxb_html_tokenizer_tags_destroy(tkz);
    lxb_html_tokenizer_destroy(tkz);

    if (err != NULL && err->message != NULL) {
        return -1;
    }

    /* Unclosed elements: report the innermost one. */
    if (ps.depth > 0) {
        open_elem top = ps.stack[ps.depth - 1];
        parser_error(&ps, "unclosed element <%.*s>",
                     (int)top.tag_len, top.tag_name);
        return -1;
    }
    return 0;
}
