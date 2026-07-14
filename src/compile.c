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
 * HTML → IR compiler, stages 1 and 2.
 *
 * Stage 1 (SAX → tree): parser.c dispatches SAX events; we maintain an
 * element stack, coalesce text chunks across last_in_text_node boundaries,
 * and attach finished elements to their parent on the matching on_endtag.
 *
 * Stage 2 (directive integration): each attribute is split by `prefix`.
 * Regular attributes stay on element.attrs verbatim. x-* attributes are
 * dispatched to the directives module and stored on element.directives.
 * Same-element combination rules (S/I/C/K exclusivity, D/W binding
 * collisions), helper name validation, and the K-only invisible_marker
 * flag are enforced here.
 *
 * Chain / match consolidation, orphan detection, and x-break context
 * checks belong to a later stage.
 */

// project
#include "compile.h"
#include "directives.h"
#include "expr/eval.h"
#include "expr/parse.h"
#include "ir.h"
#include "parser.h"
// system
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define COMPILE_MAX_DEPTH 256

/* Static buffer for compile-time error messages (single-threaded Lua). */
static char g_compile_err[512];

typedef struct {
    compile_arena *arena;
    lua_State     *L;
    const char    *html;
    size_t         html_len;
    const char    *prefix;
    size_t         prefix_len;
    const char   **helper_names;
    size_t         n_helpers;
    reflow_error  *err;

    ir_node *stack[COMPILE_MAX_DEPTH];
    size_t   depth;

    /* Text coalescing buffer for the current text run. */
    char   *text_buf;
    size_t  text_len;
    size_t  text_cap;
} compile_ctx;

static void compile_errorf(compile_ctx *cc, const char *fmt, ...)
{
    if (cc->err == NULL || cc->err->message != NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_compile_err, sizeof(g_compile_err), fmt, ap);
    va_end(ap);
    cc->err->type    = "ReflowCompileError";
    cc->err->message = g_compile_err;
}

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

/* Bit positions for directive groups (packed to fit in an unsigned). */
static unsigned group_bit(char g)
{
    switch (g) {
    case 'D': return 1u << 0;
    case 'W': return 1u << 1;
    case 'S': return 1u << 2;
    case 'I': return 1u << 3;
    case 'C': return 1u << 4;
    case 'A': return 1u << 5;
    case 'K': return 1u << 6;
    default:  return 0;
    }
}

/* --- helper set lookup --- */

static bool helper_registered(compile_ctx *cc, const char *name, size_t len)
{
    for (size_t i = 0; i < cc->n_helpers; i++) {
        const char *n = cc->helper_names[i];
        if (n != NULL && strlen(n) == len && memcmp(n, name, len) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    compile_ctx *cc;
    const char  *directive;
    bool         found_unknown;
    const char  *unknown_name;
    size_t       unknown_len;
} helper_check_ctx;

static void helper_check_cb(const char *name, size_t len, void *ud)
{
    helper_check_ctx *hc = (helper_check_ctx *)ud;
    if (hc->found_unknown) {
        return;
    }
    if (!helper_registered(hc->cc, name, len)) {
        hc->found_unknown = true;
        hc->unknown_name  = name;
        hc->unknown_len   = len;
    }
}

/* Returns 0 on success, -1 on unknown helper (error recorded). */
static int check_helpers(compile_ctx *cc, expr_node *node,
                         const char *directive)
{
    helper_check_ctx hc = {
        .cc = cc, .directive = directive, .found_unknown = false,
    };
    expr_collect_helpers(node, helper_check_cb, &hc);
    if (hc.found_unknown) {
        compile_errorf(cc, "%s: unknown helper \"%.*s\"",
                       directive, (int)hc.unknown_len, hc.unknown_name);
        return -1;
    }
    return 0;
}

/* --- x-bind:<attr> parsing --- */

static void add_bind(compile_ctx *cc, ir_directives *d,
                     const char *attr_name, size_t attr_name_len,
                     expr_node *expr)
{
    size_t new_cap = d->n_binds + 1;
    if (d->n_binds > 0) {
        new_cap = d->n_binds * 2;
    }
    ir_bind *nb = (ir_bind *)compile_arena_alloc(
        cc->arena, cc->L, new_cap * sizeof(ir_bind));
    if (d->n_binds > 0) {
        memcpy(nb, d->binds, d->n_binds * sizeof(ir_bind));
    }
    d->binds = nb;
    d->binds[d->n_binds].attr_name = dup_str(cc, attr_name, attr_name_len);
    d->binds[d->n_binds].expr      = expr;
    d->n_binds++;
}

/* Returns 0 on success, -1 on directive parse or validation failure. */
static int process_x_directive(compile_ctx *cc, ir_node *element,
                               const char *full_name, size_t full_name_len,
                               const char *value, size_t value_len,
                               unsigned *groups)
{
    ir_directives *d = &element->element.directives;
    /* suffix = full_name minus prefix */
    const char *suffix     = full_name + cc->prefix_len;
    size_t      suffix_len = full_name_len - cc->prefix_len;

    /* x-bind:<attr> */
    if (suffix_len >= 5 && memcmp(suffix, "bind:", 5) == 0) {
        const char *target = suffix + 5;
        size_t      tlen   = suffix_len - 5;
        if (tlen == 0) {
            compile_errorf(cc,
                "%.*s: attribute name after \"bind:\" is required",
                (int)full_name_len, full_name);
            return -1;
        }
        expr_node *expr = directives_parse_expr(cc->arena, cc->L,
                                                value, value_len,
                                                full_name, cc->err);
        if (expr == NULL) return -1;
        if (check_helpers(cc, expr, full_name) != 0) return -1;
        /* Reject duplicate bind:<name> */
        for (size_t i = 0; i < d->n_binds; i++) {
            if (strlen(d->binds[i].attr_name) == tlen &&
                memcmp(d->binds[i].attr_name, target, tlen) == 0) {
                compile_errorf(cc,
                    "duplicate x-bind on attribute \"%.*s\"",
                    (int)tlen, target);
                return -1;
            }
        }
        add_bind(cc, d, target, tlen, expr);
        *groups |= group_bit('A');
        return 0;
    }

    /* Known top-level directive: match against directives_is_known. */
    char nameBuf[64];
    if (suffix_len >= sizeof(nameBuf)) {
        compile_errorf(cc, "unknown directive \"%.*s\"",
                       (int)full_name_len, full_name);
        return -1;
    }
    memcpy(nameBuf, suffix, suffix_len);
    nameBuf[suffix_len] = '\0';

    if (!directives_is_known(nameBuf)) {
        compile_errorf(cc, "unknown directive \"%.*s\"",
                       (int)full_name_len, full_name);
        return -1;
    }
    char group = directives_group(nameBuf);
    if (group != 0) {
        *groups |= group_bit(group);
    }

    /* Dispatch on directive name. */
    if (strcmp(nameBuf, "data") == 0) {
        if (d->data_raw != NULL) {
            compile_errorf(cc, "duplicate x-data on element");
            return -1;
        }
        /* Validate JSON5 using a scratch arena; the raw slice is stored so
         * render-time can re-parse into the render arena. */
        char scratch[8192];
        arena_t validation;
        arena_init(&validation, scratch, sizeof(scratch));
        if (directives_parse_data(cc->arena, cc->L, &validation,
                                  value, value_len, cc->err) == NULL) {
            return -1;
        }
        d->data_raw     = dup_str(cc, value, value_len);
        d->data_raw_len = value_len;
        return 0;
    }
    if (strcmp(nameBuf, "with") == 0) {
        if (d->with_bindings != NULL) {
            compile_errorf(cc, "duplicate x-with on element");
            return -1;
        }
        ir_with_result r = directives_parse_with(cc->arena, cc->L,
                                                 value, value_len, cc->err);
        if (cc->err && cc->err->message) return -1;
        for (size_t i = 0; i < r.n; i++) {
            if (check_helpers(cc, r.bindings[i].expr, full_name) != 0)
                return -1;
        }
        d->with_bindings = r.bindings;
        d->n_with        = r.n;
        return 0;
    }
    if (strcmp(nameBuf, "if") == 0) {
        d->if_expr = directives_parse_expr(cc->arena, cc->L,
                                           value, value_len,
                                           full_name, cc->err);
        if (d->if_expr == NULL) return -1;
        return check_helpers(cc, d->if_expr, full_name);
    }
    if (strcmp(nameBuf, "elseif") == 0) {
        d->elseif_expr = directives_parse_expr(cc->arena, cc->L,
                                               value, value_len,
                                               full_name, cc->err);
        if (d->elseif_expr == NULL) return -1;
        return check_helpers(cc, d->elseif_expr, full_name);
    }
    if (strcmp(nameBuf, "else") == 0) {
        if (directives_assert_empty(value, value_len, full_name,
                                    cc->err) != 0) return -1;
        d->else_mark = true;
        return 0;
    }
    if (strcmp(nameBuf, "match") == 0) {
        d->match_expr = directives_parse_expr(cc->arena, cc->L,
                                              value, value_len,
                                              full_name, cc->err);
        if (d->match_expr == NULL) return -1;
        return check_helpers(cc, d->match_expr, full_name);
    }
    if (strcmp(nameBuf, "case") == 0) {
        d->case_expr = directives_parse_expr(cc->arena, cc->L,
                                             value, value_len,
                                             full_name, cc->err);
        if (d->case_expr == NULL) return -1;
        return check_helpers(cc, d->case_expr, full_name);
    }
    if (strcmp(nameBuf, "nocase") == 0) {
        if (directives_assert_empty(value, value_len, full_name,
                                    cc->err) != 0) return -1;
        d->nocase_mark = true;
        return 0;
    }
    if (strcmp(nameBuf, "for") == 0) {
        d->for_spec = directives_parse_for(cc->arena, cc->L,
                                           value, value_len, cc->err);
        return d->for_spec ? 0 : -1;
    }
    if (strcmp(nameBuf, "each") == 0) {
        d->each_spec = directives_parse_each(cc->arena, cc->L,
                                             value, value_len, cc->err);
        if (d->each_spec == NULL) return -1;
        return check_helpers(cc, d->each_spec->collection, full_name);
    }
    if (strcmp(nameBuf, "text") == 0) {
        d->text_expr = directives_parse_expr(cc->arena, cc->L,
                                             value, value_len,
                                             full_name, cc->err);
        if (d->text_expr == NULL) return -1;
        return check_helpers(cc, d->text_expr, full_name);
    }
    if (strcmp(nameBuf, "html") == 0) {
        d->html_expr = directives_parse_expr(cc->arena, cc->L,
                                             value, value_len,
                                             full_name, cc->err);
        if (d->html_expr == NULL) return -1;
        return check_helpers(cc, d->html_expr, full_name);
    }
    if (strcmp(nameBuf, "include") == 0) {
        d->include_expr = directives_parse_expr(cc->arena, cc->L,
                                                value, value_len,
                                                full_name, cc->err);
        if (d->include_expr == NULL) return -1;
        return check_helpers(cc, d->include_expr, full_name);
    }
    if (strcmp(nameBuf, "break") == 0) {
        if (directives_assert_empty(value, value_len, full_name,
                                    cc->err) != 0) return -1;
        d->break_mark = true;
        return 0;
    }
    if (strcmp(nameBuf, "break-if") == 0) {
        d->break_if_expr = directives_parse_expr(cc->arena, cc->L,
                                                 value, value_len,
                                                 full_name, cc->err);
        if (d->break_if_expr == NULL) return -1;
        return check_helpers(cc, d->break_if_expr, full_name);
    }
    /* directives_is_known returned true so we should never reach here. */
    compile_errorf(cc, "unknown directive \"%.*s\"",
                   (int)full_name_len, full_name);
    return -1;
}

/* --- combination validation --- */

static int validate_combinations(compile_ctx *cc, ir_directives *d,
                                 unsigned groups)
{
    int s_count = (d->if_expr ? 1 : 0) + (d->elseif_expr ? 1 : 0) +
                  (d->else_mark ? 1 : 0) + (d->match_expr ? 1 : 0) +
                  (d->case_expr ? 1 : 0) + (d->nocase_mark ? 1 : 0);
    if (s_count > 1) {
        compile_errorf(cc, "conflicting structural directives on same "
                       "element (x-if / x-elseif / x-else / x-match / "
                       "x-case / x-nocase are mutually exclusive)");
        return -1;
    }
    int i_count = (d->for_spec ? 1 : 0) + (d->each_spec ? 1 : 0);
    if (i_count > 1) {
        compile_errorf(cc, "conflicting iteration directives on same "
                       "element (x-for / x-each are mutually exclusive)");
        return -1;
    }
    int c_count = (d->text_expr ? 1 : 0) + (d->html_expr ? 1 : 0) +
                  (d->include_expr ? 1 : 0);
    if (c_count > 1) {
        compile_errorf(cc, "conflicting content directives on same "
                       "element (x-text / x-html / x-include are "
                       "mutually exclusive)");
        return -1;
    }
    int k_count = (d->break_mark ? 1 : 0) + (d->break_if_expr ? 1 : 0);
    if (k_count > 1) {
        compile_errorf(cc, "conflicting control directives on same "
                       "element (x-break / x-break-if are mutually "
                       "exclusive)");
        return -1;
    }
    /* Cross-group forbidden pairs */
    bool has_s = (groups & group_bit('S')) != 0;
    bool has_i = (groups & group_bit('I')) != 0;
    bool has_k = (groups & group_bit('K')) != 0;
    if (has_s && has_i) {
        compile_errorf(cc, "cannot combine structural directive with "
                       "iteration directive on same element; use nesting");
        return -1;
    }
    if (has_s && has_k) {
        compile_errorf(cc, "cannot combine structural directive with "
                       "control directive on same element; use nesting");
        return -1;
    }
    if (has_i && has_k) {
        compile_errorf(cc, "cannot combine iteration directive with "
                       "control directive on same element; place x-break "
                       "/ x-break-if on a child");
        return -1;
    }
    /* x-data and x-with binding-name collision */
    if (d->data_raw != NULL && d->with_bindings != NULL) {
        /* We cannot cheaply extract data names without re-parsing the raw
         * JSON5; defer this collision check to stage 3 or when x-data
         * pre-parsing lands. */
    }
    return 0;
}

static bool is_k_only(ir_directives *d, size_t n_regular_attrs)
{
    bool has_k = (d->break_mark || d->break_if_expr);
    if (!has_k) return false;
    bool has_other = (d->data_raw || d->with_bindings ||
                      d->if_expr || d->elseif_expr || d->else_mark ||
                      d->match_expr || d->case_expr || d->nocase_mark ||
                      d->for_spec || d->each_spec ||
                      d->text_expr || d->html_expr || d->include_expr ||
                      d->n_binds > 0);
    return !has_other && n_regular_attrs == 0;
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
        compile_errorf(cc, "element nesting too deep");
        return;
    }

    char *name = dup_str(cc, tag_name, tag_len);
    ir_node *node = ir_make_element(cc->arena, cc->L, name,
                                    src_start, src_end);

    unsigned groups = 0;
    for (size_t i = 0; i < n_attrs; i++) {
        const char *an = attrs[i].name;
        size_t      al = attrs[i].name_len;

        /* Split by prefix. */
        bool is_directive = (al >= cc->prefix_len &&
                             memcmp(an, cc->prefix, cc->prefix_len) == 0);
        if (!is_directive) {
            char *aname = dup_str(cc, an, al);
            char *aval = (attrs[i].value != NULL)
                         ? dup_str(cc, attrs[i].value, attrs[i].value_len)
                         : NULL;
            ir_add_attr(cc->arena, cc->L, node, aname, aval);
            continue;
        }
        if (process_x_directive(cc, node, an, al,
                                attrs[i].value,
                                attrs[i].value != NULL
                                    ? attrs[i].value_len : 0,
                                &groups) != 0) {
            return;
        }
    }

    if (validate_combinations(cc, &node->element.directives, groups) != 0) {
        return;
    }
    if (is_k_only(&node->element.directives, node->element.n_attrs)) {
        node->element.invisible_marker = true;
    }

    if (self_closing) {
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
                          const char *prefix, size_t prefix_len,
                          const char **helper_names, size_t n_helpers,
                          reflow_error *err)
{
    compile_ctx cc = {
        .arena        = arena,
        .L            = L,
        .html         = html,
        .html_len     = html_len,
        .prefix       = prefix,
        .prefix_len   = prefix_len,
        .helper_names = helper_names,
        .n_helpers    = n_helpers,
        .err          = err,
        .depth        = 0,
        .text_buf     = NULL,
        .text_len     = 0,
        .text_cap     = 0,
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

    flush_text(&cc);
    if (cc.depth != 1) {
        compile_errorf(&cc, "internal: element stack not balanced");
        return NULL;
    }
    return root;
}
