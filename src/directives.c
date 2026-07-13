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
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 *
 */

// project
#include "directives.h"
#include "expr/parse.h"
#include "json5.h"
// lua
#include <lua.h>
// system
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ── Error helper ─────────────────────────────────────────── */

static char g_dir_err[256];

static void dir_error(reflow_error *err, const char *fmt, ...)
{
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(g_dir_err, sizeof(g_dir_err), fmt, ap);
        va_end(ap);
        err->type    = "ReflowCompileError";
        err->message = g_dir_err;
    }
}

/* ── Character classification ─────────────────────────────── */

static bool is_id_start(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '_' || c == '$';
}

static bool is_id_cont(unsigned char c)
{
    return is_id_start(c) || (c >= '0' && c <= '9');
}

static void skip_ws(const char **p, const char *end)
{
    while (*p < end) {
        unsigned char c = (unsigned char)**p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            (*p)++;
        else
            break;
    }
}

static char *dup_str(compile_arena *arena, lua_State *L,
                     const char *src, size_t len)
{
    char *dst = (char *)compile_arena_alloc(arena, L, len + 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

/* ── x-data ───────────────────────────────────────────────── */

reflow_value *directives_parse_data(compile_arena *arena, lua_State *L,
                                    arena_t *rarena,
                                    const char *value, size_t len,
                                    reflow_error *err)
{
    reflow_error jerr = {0};
    reflow_value *rv = json5_parse_data(value, len, rarena, &jerr);
    if (!rv) {
        dir_error(err, "x-data: invalid JSON5: %s",
                  jerr.message ? jerr.message : "parse error");
        return NULL;
    }
    if (rv->tag != RV_OBJECT) {
        dir_error(err, "x-data: value must parse to an object");
        return NULL;
    }
    return rv;
}

/* ── x-with ───────────────────────────────────────────────── */

static size_t skip_string_lit(const char *src, size_t pos, size_t len,
                              const char *binding_name, reflow_error *err)
{
    char quote = src[pos];
    pos++;
    while (pos < len) {
        char ch = src[pos];
        if (ch == '\\' && pos + 1 < len) {
            pos += 2;
            continue;
        }
        if (ch == quote) return pos + 1;
        pos++;
    }
    dir_error(err, "x-with: unterminated string literal in binding \"%s\"",
              binding_name);
    return len;  /* will cause failure downstream */
}

ir_with_result directives_parse_with(compile_arena *arena, lua_State *L,
                                     const char *value, size_t len,
                                     reflow_error *err)
{
    ir_with_result result = { NULL, 0 };
    const char *src = value;
    const char *end = src + len;
    size_t cap = 4;
    ir_with_binding *bindings = (ir_with_binding *)
        compile_arena_alloc(arena, L, cap * sizeof(ir_with_binding));
    size_t n = 0;
    const char *p = src;

    while (true) {
        skip_ws(&p, end);
        if (p >= end) break;

        /* Binding name */
        if (!is_id_start((unsigned char)*p)) {
            dir_error(err, "x-with: expected binding name at position %zu",
                      (size_t)(p - src));
            return result;
        }
        const char *name_start = p;
        p++;
        while (p < end && is_id_cont((unsigned char)*p)) p++;
        size_t name_len = (size_t)(p - name_start);

        /* Check duplicate */
        for (size_t i = 0; i < n; i++) {
            if (strlen(bindings[i].name) == name_len &&
                memcmp(bindings[i].name, name_start, name_len) == 0) {
                char *nm = dup_str(arena, L, name_start, name_len);
                dir_error(err, "x-with: duplicate binding name \"%s\"", nm);
                return result;
            }
        }

        skip_ws(&p, end);
        if (p >= end || *p != '=') {
            char *nm = dup_str(arena, L, name_start, name_len);
            dir_error(err, "x-with: expected \"=\" after binding name \"%s\"",
                      nm);
            return result;
        }
        p++;  /* consume '=' */

        /* Scan expression slice (track bracket depth + strings) */
        skip_ws(&p, end);
        const char *expr_start = p;
        int depth = 0;
        while (p < end) {
            char ch = *p;
            if (ch == '"' || ch == '\'') {
                char *nm = dup_str(arena, L, name_start, name_len);
                p = (const char *)skip_string_lit(src, (size_t)(p - src),
                                                   len, nm, err);
                p = src + p;  /* convert back to pointer */
                if (err->message) return result;
                continue;
            }
            if (ch == '{' || ch == '[' || ch == '(') { depth++; p++; continue; }
            if (ch == '}' || ch == ']' || ch == ')') {
                if (depth == 0) {
                    char *nm = dup_str(arena, L, name_start, name_len);
                    dir_error(err,
                        "x-with: unbalanced \"%c\" in binding \"%s\"", ch, nm);
                    return result;
                }
                depth--; p++; continue;
            }
            if (ch == ',' && depth == 0) break;
            p++;
        }
        if (depth != 0) {
            char *nm = dup_str(arena, L, name_start, name_len);
            dir_error(err, "x-with: unbalanced brackets in binding \"%s\"", nm);
            return result;
        }

        /* Trim trailing whitespace from expression */
        const char *expr_end = p;
        while (expr_end > expr_start) {
            unsigned char c = (unsigned char)expr_end[-1];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                expr_end--;
            else
                break;
        }
        if (expr_end == expr_start) {
            char *nm = dup_str(arena, L, name_start, name_len);
            dir_error(err,
                "x-with: value expression is required for binding \"%s\"", nm);
            return result;
        }

        /* Parse expression */
        reflow_error perr = {0};
        expr_node *expr = expr_parse(arena, L, expr_start,
                                     (size_t)(expr_end - expr_start), &perr);
        if (!expr) {
            char *nm = dup_str(arena, L, name_start, name_len);
            dir_error(err, "x-with: %s (in binding \"%s\")",
                      perr.message ? perr.message : "parse error", nm);
            return result;
        }

        /* Grow if needed */
        if (n >= cap) {
            size_t newcap = cap * 2;
            ir_with_binding *nb = (ir_with_binding *)
                compile_arena_alloc(arena, L, newcap * sizeof(ir_with_binding));
            memcpy(nb, bindings, n * sizeof(ir_with_binding));
            bindings = nb;
            cap = newcap;
        }
        bindings[n].name = dup_str(arena, L, name_start, name_len);
        bindings[n].expr = expr;
        n++;

        skip_ws(&p, end);
        if (p >= end) break;
        if (*p != ',') {
            dir_error(err, "x-with: unexpected token \"%c\" at position %zu",
                      *p, (size_t)(p - src));
            return result;
        }
        p++;  /* consume ',' */
    }

    if (n == 0) {
        dir_error(err, "x-with: at least one binding is required");
        return result;
    }

    result.bindings = bindings;
    result.n = n;
    return result;
}

/* ── x-if / x-elseif / x-text / x-html / x-include / etc ──── */

expr_node *directives_parse_expr(compile_arena *arena, lua_State *L,
                                 const char *value, size_t len,
                                 const char *directive_name,
                                 reflow_error *err)
{
    /* Trim whitespace */
    const char *start = value;
    const char *end = value + len;
    skip_ws(&start, end);
    while (end > start) {
        unsigned char c = (unsigned char)end[-1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            end--;
        else
            break;
    }
    if (start >= end) {
        dir_error(err, "%s: value is required", directive_name);
        return NULL;
    }
    reflow_error perr = {0};
    expr_node *node = expr_parse(arena, L, start, (size_t)(end - start), &perr);
    if (!node) {
        dir_error(err, "%s: %s", directive_name,
                  perr.message ? perr.message : "parse error");
        return NULL;
    }
    return node;
}

/* ── assertEmptyValue ─────────────────────────────────────── */

int directives_assert_empty(const char *value, size_t len,
                            const char *directive_name,
                            reflow_error *err)
{
    if (value && len > 0) {
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)value[i];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                dir_error(err,
                    "%s: must not have a value; got \"%s=\"%.*s\"",
                    directive_name, directive_name, (int)len, value);
                return -1;
            }
        }
    }
    return 0;
}

/* ── x-for ────────────────────────────────────────────────── */

static int parse_int_strict(const char *s, size_t len, double *out,
                            const char *directive, reflow_error *err)
{
    /* Validate: optional '-' followed by digits only */
    size_t i = 0;
    if (i < len && s[i] == '-') i++;
    if (i >= len || !isdigit((unsigned char)s[i])) {
        dir_error(err, "%s: \"%.*s\" is not an integer",
                  directive, (int)len, s);
        return -1;
    }
    while (i < len) {
        if (!isdigit((unsigned char)s[i])) {
            dir_error(err, "%s: \"%.*s\" is not an integer",
                      directive, (int)len, s);
            return -1;
        }
        i++;
    }
    /* Parse */
    char buf[32];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    *out = (double)strtoll(buf, NULL, 10);
    return 0;
}

ir_for_spec *directives_parse_for(compile_arena *arena, lua_State *L,
                                  const char *value, size_t len,
                                  reflow_error *err)
{
    /* Find '=' */
    const char *eq = NULL;
    for (size_t i = 0; i < len; i++) {
        if (value[i] == '=') { eq = value + i; break; }
    }
    if (!eq) {
        dir_error(err, "x-for: missing \"=\", expected "
                  "\"<var> = <start>, <stop>[, <step>]\"");
        return NULL;
    }

    /* var name (trim) */
    const char *vs = value;
    const char *ve = eq;
    skip_ws(&vs, ve);
    while (ve > vs) {
        unsigned char c = (unsigned char)ve[-1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ve--;
        else break;
    }
    if (vs >= ve || !is_id_start((unsigned char)*vs)) {
        dir_error(err, "x-for: invalid variable name \"%.*s\"",
                  (int)(ve - vs), vs);
        return NULL;
    }
    /* Validate full identifier */
    const char *check = vs + 1;
    while (check < ve && is_id_cont((unsigned char)*check)) check++;
    if (check != ve) {
        dir_error(err, "x-for: invalid variable name \"%.*s\"",
                  (int)(ve - vs), vs);
        return NULL;
    }

    /* Parse comma-separated numbers after '=' */
    const char *rest = eq + 1;
    const char *end = value + len;
    double nums[3];
    int n_nums = 0;

    while (rest < end && n_nums < 3) {
        skip_ws(&rest, end);
        const char *part_start = rest;
        while (rest < end && *rest != ',') rest++;
        const char *part_end = rest;
        /* trim */
        while (part_end > part_start) {
            unsigned char c = (unsigned char)part_end[-1];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') part_end--;
            else break;
        }
        if (part_end == part_start) {
            dir_error(err, "x-for: empty argument");
            return NULL;
        }
        if (parse_int_strict(part_start, (size_t)(part_end - part_start),
                             &nums[n_nums], "x-for", err) != 0)
            return NULL;
        n_nums++;
        if (rest < end && *rest == ',') rest++;
    }

    if (n_nums < 2 || n_nums > 3) {
        dir_error(err, "x-for: expected 2 or 3 arguments after \"=\", got %d",
                  n_nums);
        return NULL;
    }

    double start = nums[0], stop = nums[1];
    double step = (n_nums == 3) ? nums[2] : 1.0;

    if (step == 0) {
        dir_error(err, "x-for: step must not be zero");
        return NULL;
    }
    if (start < stop && step < 0) {
        dir_error(err, "x-for: direction mismatch (start=%g < stop=%g "
                  "but step=%g < 0)", start, stop, step);
        return NULL;
    }
    if (start > stop && step > 0) {
        dir_error(err, "x-for: direction mismatch (start=%g > stop=%g "
                  "but step=%g > 0)", start, stop, step);
        return NULL;
    }

    ir_for_spec *spec = (ir_for_spec *)
        compile_arena_alloc(arena, L, sizeof(ir_for_spec));
    spec->var_name = dup_str(arena, L, vs, (size_t)(ve - vs));
    spec->start = start;
    spec->stop  = stop;
    spec->step  = step;
    return spec;
}

/* ── x-each ───────────────────────────────────────────────── */

ir_each_spec *directives_parse_each(compile_arena *arena, lua_State *L,
                                    const char *value, size_t len,
                                    reflow_error *err)
{
    const char *p = value;
    const char *end = value + len;

    skip_ws(&p, end);
    if (p >= end || !is_id_start((unsigned char)*p)) {
        dir_error(err, "x-each: expected \"<item>[, <index>] in <collection>"
                  "\", got \"%.*s\"", (int)len, value);
        return NULL;
    }

    /* item name */
    const char *item_start = p;
    p++;
    while (p < end && is_id_cont((unsigned char)*p)) p++;
    size_t item_len = (size_t)(p - item_start);

    /* optional index name */
    const char *index_start = NULL;
    size_t index_len = 0;
    skip_ws(&p, end);
    if (p < end && *p == ',') {
        p++;
        skip_ws(&p, end);
        if (p < end && is_id_start((unsigned char)*p)) {
            index_start = p;
            p++;
            while (p < end && is_id_cont((unsigned char)*p)) p++;
            index_len = (size_t)(p - index_start);
        }
    }

    /* "in" keyword */
    skip_ws(&p, end);
    if (p + 2 > end || !(p[0] == 'i' && p[1] == 'n')) {
        dir_error(err, "x-each: expected \"in\" keyword, got \"%.*s\"",
                  (int)len, value);
        return NULL;
    }
    /* Check word boundary after "in" */
    if (p + 2 < end && is_id_cont((unsigned char)p[2])) {
        dir_error(err, "x-each: expected \"in\" keyword, got \"%.*s\"",
                  (int)len, value);
        return NULL;
    }
    p += 2;

    /* Collection expression */
    skip_ws(&p, end);
    if (p >= end) {
        dir_error(err, "x-each: collection expression is required");
        return NULL;
    }
    const char *coll_start = p;
    const char *coll_end = end;
    /* Trim trailing whitespace */
    while (coll_end > coll_start) {
        unsigned char c = (unsigned char)coll_end[-1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') coll_end--;
        else break;
    }

    /* Check item != index */
    if (index_len > 0 && index_len == item_len &&
        memcmp(index_start, item_start, item_len) == 0) {
        dir_error(err, "x-each: item name and index name must differ");
        return NULL;
    }

    reflow_error perr = {0};
    expr_node *coll = expr_parse(arena, L, coll_start,
                                 (size_t)(coll_end - coll_start), &perr);
    if (!coll) {
        dir_error(err, "x-each: %s",
                  perr.message ? perr.message : "parse error");
        return NULL;
    }

    ir_each_spec *spec = (ir_each_spec *)
        compile_arena_alloc(arena, L, sizeof(ir_each_spec));
    spec->item_name  = dup_str(arena, L, item_start, item_len);
    spec->index_name = index_len > 0
        ? dup_str(arena, L, index_start, index_len) : NULL;
    spec->collection = coll;
    return spec;
}

/* ── Known directives ─────────────────────────────────────── */

static const char *KNOWN[] = {
    "data", "with", "if", "elseif", "else",
    "match", "case", "nocase", "for", "each",
    "text", "html", "include", "bind", "break", "break-if",
    NULL
};

bool directives_is_known(const char *name)
{
    for (int i = 0; KNOWN[i]; i++) {
        if (strcmp(name, KNOWN[i]) == 0) return true;
    }
    return false;
}

/* ── Directive groups ─────────────────────────────────────── */

char directives_group(const char *name)
{
    if (!name) return 0;
    if (strcmp(name, "data") == 0)         return 'D';
    if (strcmp(name, "with") == 0)         return 'W';
    if (strcmp(name, "if") == 0)           return 'S';
    if (strcmp(name, "elseif") == 0)       return 'S';
    if (strcmp(name, "else") == 0)         return 'S';
    if (strcmp(name, "match") == 0)        return 'S';
    if (strcmp(name, "case") == 0)         return 'S';
    if (strcmp(name, "nocase") == 0)       return 'S';
    if (strcmp(name, "for") == 0)          return 'I';
    if (strcmp(name, "each") == 0)         return 'I';
    if (strcmp(name, "text") == 0)         return 'C';
    if (strcmp(name, "html") == 0)         return 'C';
    if (strcmp(name, "include") == 0)      return 'C';
    if (strcmp(name, "bind") == 0)         return 'A';
    if (strcmp(name, "break") == 0)        return 'K';
    if (strcmp(name, "break-if") == 0)     return 'K';
    return 0;
}
