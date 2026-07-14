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
#include "parse.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Overflow guard for pseudo-arg accumulation. */
#define LONG_MAX_SAFE ((long)((~(unsigned long)0) >> 2))

static const char *const ATTR_OP_STR[] = {
    NULL, "=", "~=", "|=", "^=", "$=", "*=",
};

/* Persistent buffer for the most recently formatted error message so the
 * const char* stored on reflow_error stays valid after the parser returns.
 * Single-threaded consumers only (mirrors g_compile_err in compile.c). */
static char g_selector_err[512];
static char g_selector_source[1024];
static char g_selector_feature[128];

static const char *POSITIONAL_PSEUDOS_LC[] = {
    "first-child", "last-child", "only-child",
    "first-of-type", "last-of-type", "only-of-type",
    "nth-child", "nth-last-child", "nth-of-type", "nth-last-of-type",
    NULL,
};

typedef struct {
    const char    *source;
    size_t         source_len;
    size_t         pos;
    compile_arena *arena;
    lua_State     *L;
    reflow_error  *err;
} parser_t;

/* Growable pointer array while parsing; committed to arena once size is
 * known. Grows on the C heap so a runaway selector doesn't blow the
 * bump-allocated arena chunk repeatedly. */
typedef struct {
    void   *items;
    size_t  count;
    size_t  cap;
    size_t  elem_size;
} plist_t;

static int plist_init(plist_t *l, size_t elem_size)
{
    l->items     = NULL;
    l->count     = 0;
    l->cap       = 0;
    l->elem_size = elem_size;
    return 0;
}

static void plist_free(plist_t *l)
{
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap   = 0;
}

static int plist_push(plist_t *l, const void *elem)
{
    if (l->count == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 4;
        void  *nb   = realloc(l->items, ncap * l->elem_size);
        if (!nb) return -1;
        l->items = nb;
        l->cap   = ncap;
    }
    memcpy((char *)l->items + l->count * l->elem_size, elem, l->elem_size);
    l->count++;
    return 0;
}

/* Copy the collected elements into arena and free the heap buffer. */
static void *plist_commit(plist_t *l, compile_arena *a, lua_State *L)
{
    if (l->count == 0) {
        plist_free(l);
        return NULL;
    }
    size_t bytes = l->count * l->elem_size;
    void  *dst   = compile_arena_alloc(a, L, bytes);
    memcpy(dst, l->items, bytes);
    plist_free(l);
    return dst;
}

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '-';
}

static int is_ident_part(char c)
{
    return is_ident_start(c) || is_digit(c);
}

static int p_eof(const parser_t *p)
{
    return p->pos >= p->source_len;
}

/* Returns the current byte or '\0' when past end. Callers must guard
 * against '\0' being a legitimate character; selectors are ASCII-clean
 * so '\0' here always means EOF. */
static char p_peek(const parser_t *p)
{
    return p_eof(p) ? '\0' : p->source[p->pos];
}

static char p_peek_at(const parser_t *p, size_t off)
{
    size_t k = p->pos + off;
    return k >= p->source_len ? '\0' : p->source[k];
}

static char p_next(parser_t *p)
{
    char c = p_peek(p);
    if (!p_eof(p)) p->pos++;
    return c;
}

static void p_skip_ws(parser_t *p)
{
    while (!p_eof(p) && is_ws(p->source[p->pos])) p->pos++;
}

/* Consume ws while returning whether any was consumed — used to decide
 * whether an implicit descendant combinator should be emitted. */
static int p_skip_combinator_ws(parser_t *p)
{
    size_t before = p->pos;
    p_skip_ws(p);
    return p->pos > before;
}

/* Snapshot the parser source into the persistent buffer so reflow_error's
 * const char* pointers stay valid after the parser returns. Truncates on
 * overflow — the truncated view is still useful for humans. */
static const char *snapshot_source(const char *src, size_t src_len)
{
    size_t n = src_len < sizeof(g_selector_source) - 1
               ? src_len : sizeof(g_selector_source) - 1;
    memcpy(g_selector_source, src, n);
    g_selector_source[n] = '\0';
    return g_selector_source;
}

static const char *snapshot_feature(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s);
    if (n >= sizeof(g_selector_feature)) n = sizeof(g_selector_feature) - 1;
    memcpy(g_selector_feature, s, n);
    g_selector_feature[n] = '\0';
    return g_selector_feature;
}

/* Compute 1-based line/column for a 0-based offset in source. */
static void offset_to_linecol(const char *src, size_t src_len, size_t off,
                              long *out_line, long *out_col)
{
    long line = 1, col = 1;
    if (off > src_len) off = src_len;
    for (size_t i = 0; i < off; i++) {
        if (src[i] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    *out_line = line;
    *out_col  = col;
}

/* Format an error into g_selector_err and fill err with the given reason,
 * plus a source snapshot and 0-based position. Returns 0 for convenience. */
static int fail(parser_t *p, const char *reason, size_t position,
                const char *feature, const char *fmt, ...)
{
    if (p->err == NULL) return -1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_selector_err, sizeof(g_selector_err), fmt, ap);
    va_end(ap);
    p->err->type     = "ReflowSelectorError";
    p->err->message  = g_selector_err;
    p->err->reason   = reason;
    p->err->source   = snapshot_source(p->source, p->source_len);
    p->err->position = (long)position;
    p->err->feature  = feature ? snapshot_feature(feature) : NULL;
    long line, col;
    offset_to_linecol(p->source, p->source_len, position, &line, &col);
    p->err->line   = line;
    p->err->column = col;
    return -1;
}

/* Duplicate `len` bytes from `src` into the arena as a NUL-terminated
 * lowercase copy. Returns NULL on OOM (arena throws to nearest pcall). */
static const char *arena_lowerdup(compile_arena *a, lua_State *L,
                                  const char *src, size_t len)
{
    char *dst = (char *)compile_arena_alloc(a, L, len + 1);
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        dst[i] = c;
    }
    dst[len] = '\0';
    return dst;
}

/* Same as arena_lowerdup but preserves case — used for attribute values. */
static const char *arena_strdup(compile_arena *a, lua_State *L,
                                const char *src, size_t len)
{
    char *dst = (char *)compile_arena_alloc(a, L, len + 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

/* Read an identifier; returns 0 and fills out_start / out_len.
 * Errors otherwise. */
static int read_ident(parser_t *p, const char **out_start, size_t *out_len)
{
    if (!is_ident_start(p_peek(p))) {
        return fail(p, "syntax", p->pos, NULL,
                    "selector syntax error: expected identifier");
    }
    *out_start = p->source + p->pos;
    p_next(p);
    while (!p_eof(p) && is_ident_part(p->source[p->pos])) p->pos++;
    *out_len = (size_t)(p->source + p->pos - *out_start);
    return 0;
}

/* Read a quoted string (opening quote already peeked) into a heap buffer
 * so escapes can be expanded, then copy into arena and free the buffer.
 * On success, returns 0 and writes to *out_value / *out_len. */
static int read_string(parser_t *p, char quote,
                       const char **out_value, size_t *out_len)
{
    /* consume opening quote */
    p_next(p);
    char  *buf   = NULL;
    size_t bcap  = 0;
    size_t blen  = 0;
    while (!p_eof(p)) {
        char c = p_next(p);
        if (c == quote) {
            const char *dst = arena_strdup(p->arena, p->L, buf ? buf : "",
                                           blen);
            free(buf);
            *out_value = dst;
            *out_len   = blen;
            return 0;
        }
        if (c == '\\') {
            if (p_eof(p)) {
                free(buf);
                return fail(p, "syntax", p->pos, NULL,
                            "selector syntax error: "
                            "unterminated string escape");
            }
            c = p_next(p);
        } else if (c == '\n' || c == '\r') {
            free(buf);
            return fail(p, "syntax", p->pos - 1, NULL,
                        "selector syntax error: "
                        "unterminated string literal");
        }
        if (blen + 1 > bcap) {
            size_t ncap = bcap ? bcap * 2 : 32;
            char  *nb   = (char *)realloc(buf, ncap);
            if (!nb) {
                free(buf);
                return fail(p, "syntax", p->pos, NULL,
                            "selector syntax error: out of memory");
            }
            buf  = nb;
            bcap = ncap;
        }
        buf[blen++] = c;
    }
    free(buf);
    return fail(p, "syntax", p->pos, NULL,
                "selector syntax error: unterminated string literal");
}

static int is_positional_pseudo(const char *name, sel_pseudo_name *out)
{
    for (int i = 0; POSITIONAL_PSEUDOS_LC[i] != NULL; i++) {
        if (strcmp(POSITIONAL_PSEUDOS_LC[i], name) == 0) {
            *out = (sel_pseudo_name)i;
            return 1;
        }
    }
    return 0;
}

static int pseudo_requires_arg(sel_pseudo_name n)
{
    return n == SEL_PSEUDO_NTH_CHILD || n == SEL_PSEUDO_NTH_LAST_CHILD ||
           n == SEL_PSEUDO_NTH_OF_TYPE || n == SEL_PSEUDO_NTH_LAST_OF_TYPE;
}

/* --- attribute: `[ name ( op value )? ]` -------------------------------- */
static int parse_attr(parser_t *p, sel_attr_cond *out)
{
    /* opening [ */
    p_next(p);
    p_skip_ws(p);
    if (!is_ident_start(p_peek(p))) {
        return fail(p, "syntax", p->pos, NULL,
                    "selector syntax error: expected attribute name");
    }
    size_t      attr_start = p->pos;
    const char *nptr;
    size_t      nlen;
    if (read_ident(p, &nptr, &nlen) != 0) return -1;
    /* Namespace `ns|name` is only rejected when `|` is not part of `|=`
     * and not the column combinator `||`. Match JS logic exactly. */
    if (p_peek(p) == '|' && p_peek_at(p, 1) != '=' && p_peek_at(p, 1) != '|') {
        return fail(p, "unsupported", attr_start, "attr-namespace",
                    "selector unsupported: "
                    "attribute namespaces are not supported");
    }
    /* attribute names are case-insensitive in HTML */
    out->name     = arena_lowerdup(p->arena, p->L, nptr, nlen);
    out->name_len = nlen;
    out->op       = SEL_ATTR_OP_NONE;
    out->value    = NULL;
    out->value_len = 0;

    p_skip_ws(p);
    if (p_peek(p) != ']') {
        char c0 = p_peek(p);
        if (c0 == '=') {
            out->op = SEL_ATTR_OP_EQ;
            p_next(p);
        } else if (c0 == '~' || c0 == '|' || c0 == '^' || c0 == '$' ||
                   c0 == '*') {
            if (p_peek_at(p, 1) != '=') {
                return fail(p, "syntax", p->pos + 1, NULL,
                            "selector syntax error: "
                            "expected \"=\" after \"%c\" "
                            "in attribute selector",
                            c0);
            }
            switch (c0) {
            case '~': out->op = SEL_ATTR_OP_TILDE; break;
            case '|': out->op = SEL_ATTR_OP_PIPE; break;
            case '^': out->op = SEL_ATTR_OP_CARET; break;
            case '$': out->op = SEL_ATTR_OP_DOLLAR; break;
            case '*': out->op = SEL_ATTR_OP_STAR; break;
            }
            p->pos += 2;
        } else {
            return fail(p, "syntax", p->pos, NULL,
                        "selector syntax error: "
                        "expected attribute operator or \"]\"");
        }
        p_skip_ws(p);
        char vc = p_peek(p);
        if (vc == '"' || vc == '\'') {
            if (read_string(p, vc, &out->value, &out->value_len) != 0)
                return -1;
        } else if (is_ident_start(vc)) {
            const char *vptr;
            size_t      vlen;
            if (read_ident(p, &vptr, &vlen) != 0) return -1;
            out->value     = arena_strdup(p->arena, p->L, vptr, vlen);
            out->value_len = vlen;
        } else {
            return fail(p, "syntax", p->pos, NULL,
                        "selector syntax error: "
                        "expected attribute value after \"%s\"",
                        ATTR_OP_STR[out->op]);
        }
        p_skip_ws(p);
        char flag = p_peek(p);
        if (flag == 'i' || flag == 'I' || flag == 's' || flag == 'S') {
            return fail(p, "unsupported", p->pos, "attr-case-flag",
                        "selector unsupported: "
                        "attribute case-sensitivity flag is not supported");
        }
    }
    if (p_peek(p) != ']') {
        return fail(p, "syntax", p->pos, NULL,
                    "selector syntax error: "
                    "expected \"]\" to close attribute selector");
    }
    p_next(p);
    return 0;
}

/* --- pseudo-class: `:name` or `:name( int )` --------------------------- */
static int parse_pseudo(parser_t *p, sel_pseudo_cond *out)
{
    /* consume ':' */
    p_next(p);
    if (!is_ident_start(p_peek(p))) {
        return fail(p, "syntax", p->pos, NULL,
                    "selector syntax error: "
                    "expected pseudo-class name after \":\"");
    }
    size_t      name_start = p->pos;
    const char *nptr;
    size_t      nlen;
    if (read_ident(p, &nptr, &nlen) != 0) return -1;

    const char     *name = arena_lowerdup(p->arena, p->L, nptr, nlen);
    sel_pseudo_name kind;
    if (!is_positional_pseudo(name, &kind)) {
        char feature[128];
        snprintf(feature, sizeof(feature), "pseudo:%s", name);
        return fail(p, "unsupported", name_start - 1, feature,
                    "selector unsupported: "
                    "pseudo-class \":%.*s\" is not supported "
                    "(supported: :first-child, :last-child, :only-child, "
                    ":first-of-type, :last-of-type, :only-of-type, "
                    ":nth-child(n), :nth-last-child(n), :nth-of-type(n), "
                    ":nth-last-of-type(n))",
                    (int)nlen, nptr);
    }
    out->name = kind;
    out->n    = 0;

    if (!pseudo_requires_arg(kind)) {
        if (p_peek(p) == '(') {
            return fail(p, "syntax", p->pos, NULL,
                        "selector syntax error: "
                        "\":%s\" does not take an argument",
                        name);
        }
        return 0;
    }
    if (p_peek(p) != '(') {
        return fail(p, "syntax", p->pos, NULL,
                    "selector syntax error: "
                    "\":%s\" requires an integer argument, "
                    "e.g. \":%s(3)\"",
                    name, name);
    }
    p_next(p);
    p_skip_ws(p);
    size_t num_start = p->pos;
    if (!is_digit(p_peek(p))) {
        char feature[128];
        snprintf(feature, sizeof(feature), "pseudo-arg:%s", name);
        return fail(p, "unsupported", p->pos, feature,
                    "selector unsupported: "
                    "\":%s\" accepts only a positive integer literal; "
                    "formulas (An+B, odd, even) are not supported",
                    name);
    }
    long n = 0;
    while (is_digit(p_peek(p))) {
        long d = (long)(p_next(p) - '0');
        /* Guard against overflow — clamp on the low side so subsequent
         * syntax check catches implausibly large arguments as invalid. */
        if (n > (LONG_MAX_SAFE - d) / 10) {
            n = LONG_MAX_SAFE;
        } else {
            n = n * 10 + d;
        }
    }
    p_skip_ws(p);
    if (p_peek(p) != ')') {
        char feature[128];
        snprintf(feature, sizeof(feature), "pseudo-arg:%s", name);
        return fail(p, "unsupported", num_start, feature,
                    "selector unsupported: "
                    "\":%s\" accepts only a positive integer literal; "
                    "formulas (An+B, odd, even) are not supported",
                    name);
    }
    p_next(p);
    if (n < 1) {
        return fail(p, "syntax", num_start, NULL,
                    "selector syntax error: "
                    "\":%s(n)\" argument must be a positive integer (>= 1)",
                    name);
    }
    out->n = n;
    return 0;
}

/* --- compound: `(tag|*)?( #id | .class | [attr] | :pseudo )*` ---------- */
static int parse_compound(parser_t *p, sel_compound *out)
{
    memset(out, 0, sizeof(*out));
    int count = 0;

    plist_t classes, attrs, pseudos;
    plist_init(&classes, sizeof(const char *));
    /* class_lens shadows classes; we build it in parallel */
    plist_t class_lens;
    plist_init(&class_lens, sizeof(size_t));
    plist_init(&attrs, sizeof(sel_attr_cond));
    plist_init(&pseudos, sizeof(sel_pseudo_cond));

    if (!p_eof(p) && p_peek(p) == '*') {
        p_next(p);
        /* universal — tag stays NULL */
        count++;
    } else if (!p_eof(p) && is_ident_start(p_peek(p))) {
        const char *nptr;
        size_t      nlen;
        if (read_ident(p, &nptr, &nlen) != 0) goto err;
        out->tag     = arena_lowerdup(p->arena, p->L, nptr, nlen);
        out->tag_len = nlen;
        count++;
    }

    while (!p_eof(p)) {
        char ch = p_peek(p);
        if (ch == '#') {
            p_next(p);
            if (!is_ident_start(p_peek(p))) {
                fail(p, "syntax", p->pos, NULL,
                     "selector syntax error: expected identifier after \"#\"");
                goto err;
            }
            if (out->id != NULL) {
                fail(p, "syntax", p->pos, NULL,
                     "selector syntax error: "
                     "multiple \"#id\" in the same compound selector");
                goto err;
            }
            const char *iptr;
            size_t      ilen;
            if (read_ident(p, &iptr, &ilen) != 0) goto err;
            out->id     = arena_strdup(p->arena, p->L, iptr, ilen);
            out->id_len = ilen;
            count++;
            continue;
        }
        if (ch == '.') {
            p_next(p);
            if (!is_ident_start(p_peek(p))) {
                fail(p, "syntax", p->pos, NULL,
                     "selector syntax error: expected identifier after \".\"");
                goto err;
            }
            const char *cptr;
            size_t      clen;
            if (read_ident(p, &cptr, &clen) != 0) goto err;
            const char *cdup = arena_strdup(p->arena, p->L, cptr, clen);
            if (plist_push(&classes, &cdup) != 0 ||
                plist_push(&class_lens, &clen) != 0) {
                fail(p, "syntax", p->pos, NULL,
                     "selector syntax error: out of memory");
                goto err;
            }
            count++;
            continue;
        }
        if (ch == '[') {
            sel_attr_cond ac;
            if (parse_attr(p, &ac) != 0) goto err;
            if (plist_push(&attrs, &ac) != 0) {
                fail(p, "syntax", p->pos, NULL,
                     "selector syntax error: out of memory");
                goto err;
            }
            count++;
            continue;
        }
        if (ch == ':') {
            if (p_peek_at(p, 1) == ':') {
                fail(p, "unsupported", p->pos, "pseudo-element",
                     "selector unsupported: "
                     "pseudo-elements (\"::\") are not supported");
                goto err;
            }
            sel_pseudo_cond pc;
            if (parse_pseudo(p, &pc) != 0) goto err;
            if (plist_push(&pseudos, &pc) != 0) {
                fail(p, "syntax", p->pos, NULL,
                     "selector syntax error: out of memory");
                goto err;
            }
            count++;
            continue;
        }
        break;
    }

    if (count == 0) {
        fail(p, "syntax", p->pos, NULL,
             "selector syntax error: expected selector");
        goto err;
    }

    out->n_classes  = classes.count;
    out->classes    = (const char **)plist_commit(&classes, p->arena, p->L);
    out->class_lens = (size_t *)plist_commit(&class_lens, p->arena, p->L);
    out->n_attrs    = attrs.count;
    out->attrs      = (sel_attr_cond *)plist_commit(&attrs, p->arena, p->L);
    out->n_pseudos  = pseudos.count;
    out->pseudos    = (sel_pseudo_cond *)plist_commit(&pseudos, p->arena,
                                                      p->L);
    return 0;

err:
    plist_free(&classes);
    plist_free(&class_lens);
    plist_free(&attrs);
    plist_free(&pseudos);
    return -1;
}

/* --- complex: `Compound (Combinator Compound)*` ------------------------ */
static int parse_complex(parser_t *p, sel_complex *out)
{
    memset(out, 0, sizeof(*out));
    plist_t parts;
    plist_init(&parts, sizeof(sel_complex_part));

    p_skip_ws(p);
    sel_complex_part first;
    first.combinator = SEL_COMB_NONE;
    if (parse_compound(p, &first.compound) != 0) goto err;
    if (plist_push(&parts, &first) != 0) {
        fail(p, "syntax", p->pos, NULL,
             "selector syntax error: out of memory");
        goto err;
    }

    for (;;) {
        int  had_ws = p_skip_combinator_ws(p);
        char ch     = p_peek(p);
        if (p_eof(p) || ch == ',') break;

        sel_combinator comb;
        if (ch == '>') {
            p_next(p);
            p_skip_ws(p);
            comb = SEL_COMB_CHILD;
        } else if (ch == '+' || ch == '~') {
            char buf[3] = {ch, 0, 0};
            char feat[32];
            snprintf(feat, sizeof(feat), "combinator:%s", buf);
            fail(p, "unsupported", p->pos, feat,
                 "selector unsupported: "
                 "combinator \"%c\" is not supported "
                 "(only descendant \" \" and child \">\" are supported)",
                 ch);
            goto err;
        } else if (ch == '|' && p_peek_at(p, 1) == '|') {
            fail(p, "unsupported", p->pos, "combinator:||",
                 "selector unsupported: "
                 "column combinator \"||\" is not supported");
            goto err;
        } else if (had_ws) {
            comb = SEL_COMB_DESCENDANT;
        } else {
            break;
        }
        sel_complex_part next;
        next.combinator = comb;
        if (parse_compound(p, &next.compound) != 0) goto err;
        if (plist_push(&parts, &next) != 0) {
            fail(p, "syntax", p->pos, NULL,
                 "selector syntax error: out of memory");
            goto err;
        }
    }
    out->n_parts = parts.count;
    out->parts   = (sel_complex_part *)plist_commit(&parts, p->arena, p->L);
    return 0;

err:
    plist_free(&parts);
    return -1;
}

/* Wrap `long` overflow guard for pseudo-arg accumulation is defined
 * near the top of the file so it is visible in parse_pseudo. */

sel_compiled *selector_parse(compile_arena *arena, lua_State *L,
                             const char *source, size_t source_len,
                             reflow_error *err)
{
    /* Trim-check: empty or whitespace-only source is a syntax error. */
    size_t first = 0;
    while (first < source_len && is_ws(source[first])) first++;
    if (first == source_len) {
        parser_t p = {source, source_len, 0, arena, L, err};
        fail(&p, "syntax", 0, NULL, "selector syntax error: empty selector");
        return NULL;
    }

    parser_t p = {source, source_len, 0, arena, L, err};

    plist_t sels;
    plist_init(&sels, sizeof(sel_complex));

    sel_complex first_c;
    if (parse_complex(&p, &first_c) != 0) {
        plist_free(&sels);
        return NULL;
    }
    if (plist_push(&sels, &first_c) != 0) {
        fail(&p, "syntax", p.pos, NULL,
             "selector syntax error: out of memory");
        plist_free(&sels);
        return NULL;
    }
    while (p_peek(&p) == ',') {
        p_next(&p);
        p_skip_ws(&p);
        sel_complex next;
        if (parse_complex(&p, &next) != 0) {
            plist_free(&sels);
            return NULL;
        }
        if (plist_push(&sels, &next) != 0) {
            fail(&p, "syntax", p.pos, NULL,
                 "selector syntax error: out of memory");
            plist_free(&sels);
            return NULL;
        }
    }
    p_skip_ws(&p);
    if (!p_eof(&p)) {
        fail(&p, "syntax", p.pos, NULL,
             "selector syntax error: unexpected \"%c\"", p_peek(&p));
        plist_free(&sels);
        return NULL;
    }

    sel_compiled *out =
        (sel_compiled *)compile_arena_alloc(arena, L, sizeof(*out));
    out->source     = arena_strdup(arena, L, source, source_len);
    out->source_len = source_len;
    out->n_selectors = sels.count;
    out->selectors   = (sel_complex *)plist_commit(&sels, arena, L);
    out->has_positional = false;
    for (size_t i = 0; i < out->n_selectors; i++) {
        for (size_t j = 0; j < out->selectors[i].n_parts; j++) {
            if (out->selectors[i].parts[j].compound.n_pseudos > 0) {
                out->has_positional = true;
                break;
            }
        }
        if (out->has_positional) break;
    }
    return out;
}
