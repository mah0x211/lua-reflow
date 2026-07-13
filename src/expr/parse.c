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
/*
 * Expression parser for reflow.
 *
 * All AST memory is bump-allocated from a compile_arena whose chunks
 * are GC-tracked lua_newuserdata on a dedicated thread's stack.
 * No malloc/free — no leak on longjmp, no individual cleanup.
 */

// project
#include "../compile_arena.h"
#include "parse.h"
#include "../value.h"
// lua
#include <lua.h>
#include <lauxlib.h>
// system
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================ */
/* Character classification                                      */
/* ============================================================ */

static bool is_id_start(unsigned char c)
{
    return (c >= 0x41 && c <= 0x5a) ||
           (c >= 0x61 && c <= 0x7a) ||
           c == 0x5f || c == 0x24;
}

static bool is_id_cont(unsigned char c)
{
    return is_id_start(c) || (c >= 0x30 && c <= 0x39);
}

static bool is_digit(unsigned char c)
{
    return c >= 0x30 && c <= 0x39;
}

/* ============================================================ */
/* Parser state                                                  */
/* ============================================================ */

typedef struct {
    const char    *src;
    size_t         len;
    size_t         pos;
    reflow_error  *err;
    bool           has_error;
    compile_arena *arena;
    lua_State     *L;
} parser_t;

/* Static buffer for error messages (single-threaded Lua, no leak). */
static char g_err_msg[256];

static void parse_error(parser_t *p, const char *msg)
{
    if (p->err && !p->has_error) {
        p->has_error = true;
        snprintf(g_err_msg, sizeof(g_err_msg),
                 "expression parse error at position %zu: %s", p->pos, msg);
        p->err->type    = "ReflowCompileError";
        p->err->message = g_err_msg;
        p->err->column  = (long)(p->pos + 1);
    }
}

static void parse_errorf(parser_t *p, const char *fmt, ...)
{
    if (p->err && !p->has_error) {
        char detail[200];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, sizeof(detail), fmt, ap);
        va_end(ap);
        p->has_error = true;
        snprintf(g_err_msg, sizeof(g_err_msg),
                 "expression parse error at position %zu: %s", p->pos, detail);
        p->err->type    = "ReflowCompileError";
        p->err->message = g_err_msg;
        p->err->column  = (long)(p->pos + 1);
    }
}

/* ============================================================ */
/* Parser helpers                                                */
/* ============================================================ */

static void skip_ws(parser_t *p)
{
    while (p->pos < p->len) {
        unsigned char ch = (unsigned char)p->src[p->pos];
        if (ch == 0x20 || ch == 0x09 || ch == 0x0a || ch == 0x0d)
            p->pos++;
        else
            break;
    }
}

static bool starts_with(parser_t *p, const char *str, size_t slen)
{
    skip_ws(p);
    if (p->pos + slen > p->len) return false;
    return memcmp(p->src + p->pos, str, slen) == 0;
}

static bool consume_str(parser_t *p, const char *str, size_t slen)
{
    if (starts_with(p, str, slen)) {
        p->pos += slen;
        return true;
    }
    return false;
}

static bool expect_str(parser_t *p, const char *str, size_t slen)
{
    if (consume_str(p, str, slen)) return true;
    parse_errorf(p, "expected \"%s\"", str);
    return false;
}

static size_t mark(parser_t *p)
{
    skip_ws(p);
    return p->pos;
}

/* ============================================================ */
/* Arena-backed node allocation                                  */
/* ============================================================ */

static expr_node *new_node(parser_t *p, enum ex_type type, size_t offset)
{
    expr_node *n = (expr_node *)compile_arena_alloc(p->arena, p->L,
                                                     sizeof(expr_node));
    memset(n, 0, sizeof(expr_node));
    n->type   = type;
    n->offset = offset;
    return n;
}

/* ============================================================ */
/* Arena-backed dynamic arrays                                    */
/* ============================================================ */

typedef struct { expr_node **items; size_t len, cap; } nodevec_t;

static void nodevec_init(nodevec_t *v, parser_t *p)
{
    v->cap   = 4;
    v->len   = 0;
    v->items = (expr_node **)compile_arena_alloc(p->arena, p->L,
                                     v->cap * sizeof(expr_node *));
}

static void nodevec_push(nodevec_t *v, parser_t *p, expr_node *node)
{
    if (v->len >= v->cap) {
        size_t newcap = v->cap * 2;
        expr_node **ni = (expr_node **)compile_arena_alloc(p->arena, p->L,
                                            newcap * sizeof(expr_node *));
        memcpy(ni, v->items, v->len * sizeof(expr_node *));
        v->items = ni;
        v->cap   = newcap;
    }
    v->items[v->len++] = node;
}

typedef struct { obj_entry *entries; size_t len, cap; } entryvec_t;

static void entryvec_init(entryvec_t *v, parser_t *p)
{
    v->cap     = 4;
    v->len     = 0;
    v->entries = (obj_entry *)compile_arena_alloc(p->arena, p->L,
                                     v->cap * sizeof(obj_entry));
}

static void entryvec_push(entryvec_t *v, parser_t *p, obj_entry *e)
{
    if (v->len >= v->cap) {
        size_t newcap = v->cap * 2;
        obj_entry *ne = (obj_entry *)compile_arena_alloc(p->arena, p->L,
                                            newcap * sizeof(obj_entry));
        memcpy(ne, v->entries, v->len * sizeof(obj_entry));
        v->entries = ne;
        v->cap     = newcap;
    }
    v->entries[v->len++] = *e;
}

/* ============================================================ */
/* Forward declarations                                          */
/* ============================================================ */

static expr_node *parse_expr(parser_t *p);
static expr_node *parse_ternary(parser_t *p);
static expr_node *parse_coalesce(parser_t *p);
static expr_node *parse_logical_or(parser_t *p);
static expr_node *parse_logical_and(parser_t *p);
static expr_node *parse_comparison(parser_t *p);
static expr_node *parse_unary(parser_t *p);
static expr_node *parse_postfix(parser_t *p);
static expr_node *parse_primary(parser_t *p);
static expr_node *parse_object_literal(parser_t *p, size_t start);
static expr_node *parse_array_literal(parser_t *p, size_t start);
static expr_node *read_string_literal(parser_t *p, size_t start);
static expr_node *read_number_literal(parser_t *p, size_t start);
static char      *read_identifier(parser_t *p);

/* ============================================================ */
/* Grammar                                                       */
/* ============================================================ */

static expr_node *parse_expr(parser_t *p)
{
    return parse_ternary(p);
}

static expr_node *parse_ternary(parser_t *p)
{
    size_t start = mark(p);
    expr_node *cond = parse_coalesce(p);
    if (!cond) return NULL;

    if (consume_str(p, "?", 1)) {
        expr_node *cons = parse_expr(p);
        if (!cons) return NULL;

        if (!expect_str(p, ":", 1)) return NULL;

        expr_node *alt = parse_expr(p);
        if (!alt) return NULL;

        expr_node *node = new_node(p, EX_TERNARY, start);
        node->ternary.test = cond;
        node->ternary.cons = cons;
        node->ternary.alt  = alt;
        return node;
    }
    return cond;
}

static expr_node *parse_coalesce(parser_t *p)
{
    expr_node *left = parse_logical_or(p);
    if (!left) return NULL;

    while (starts_with(p, "??", 2)) {
        size_t start = left->offset;
        p->pos += 2;
        expr_node *right = parse_logical_or(p);
        if (!right) return NULL;

        expr_node *node = new_node(p, EX_BINARY, start);
        node->binary.op    = OP_COALESCE;
        node->binary.left  = left;
        node->binary.right = right;
        left = node;
    }
    return left;
}

static expr_node *parse_logical_or(parser_t *p)
{
    expr_node *left = parse_logical_and(p);
    if (!left) return NULL;

    while (starts_with(p, "||", 2)) {
        size_t start = left->offset;
        p->pos += 2;
        expr_node *right = parse_logical_and(p);
        if (!right) return NULL;

        expr_node *node = new_node(p, EX_BINARY, start);
        node->binary.op    = OP_OR;
        node->binary.left  = left;
        node->binary.right = right;
        left = node;
    }
    return left;
}

static expr_node *parse_logical_and(parser_t *p)
{
    expr_node *left = parse_comparison(p);
    if (!left) return NULL;

    while (starts_with(p, "&&", 2)) {
        size_t start = left->offset;
        p->pos += 2;
        expr_node *right = parse_comparison(p);
        if (!right) return NULL;

        expr_node *node = new_node(p, EX_BINARY, start);
        node->binary.op    = OP_AND;
        node->binary.left  = left;
        node->binary.right = right;
        left = node;
    }
    return left;
}

static expr_node *parse_comparison(parser_t *p)
{
    size_t start = mark(p);
    expr_node *left = parse_unary(p);
    if (!left) return NULL;

    skip_ws(p);
    enum bin_op op;
    bool have_op = false;

    if (starts_with(p, "==", 2)) { op = OP_EQ; p->pos += 2; have_op = true; }
    else if (starts_with(p, "!=", 2)) { op = OP_NE; p->pos += 2; have_op = true; }
    else if (starts_with(p, "<=", 2)) { op = OP_LE; p->pos += 2; have_op = true; }
    else if (starts_with(p, ">=", 2)) { op = OP_GE; p->pos += 2; have_op = true; }
    else if (starts_with(p, "<", 1)) { op = OP_LT; p->pos += 1; have_op = true; }
    else if (starts_with(p, ">", 1)) { op = OP_GT; p->pos += 1; have_op = true; }

    if (have_op) {
        expr_node *right = parse_unary(p);
        if (!right) return NULL;

        expr_node *node = new_node(p, EX_BINARY, start);
        node->binary.op    = op;
        node->binary.left  = left;
        node->binary.right = right;
        return node;
    }
    return left;
}

static expr_node *parse_unary(parser_t *p)
{
    skip_ws(p);
    if (starts_with(p, "!", 1)) {
        if (p->pos + 1 < p->len && p->src[p->pos + 1] == '=') {
            return parse_postfix(p);
        }
        size_t start = p->pos;
        p->pos++;
        expr_node *arg = parse_unary(p);
        if (!arg) return NULL;

        expr_node *node = new_node(p, EX_UNARY, start);
        node->unary.arg = arg;
        return node;
    }
    return parse_postfix(p);
}

static expr_node *parse_postfix(parser_t *p)
{
    size_t start = mark(p);
    expr_node *node = parse_primary(p);
    if (!node) return NULL;

    while (true) {
        skip_ws(p);
        if (starts_with(p, "?.", 2)) {
            p->pos += 2;
            char *name = read_identifier(p);
            if (!name) {
                parse_error(p, "expected identifier after \"?.\"");
                return NULL;
            }
            expr_node *m = new_node(p, EX_MEMBER, start);
            m->member.object   = node;
            m->member.pname    = name;
            m->member.optional = true;
            node = m;
        } else if (starts_with(p, ".", 1)) {
            unsigned char nc = (p->pos + 1 < p->len)
                             ? (unsigned char)p->src[p->pos + 1] : 0;
            if (!is_id_start(nc)) break;
            p->pos++;
            char *name = read_identifier(p);
            if (!name) return NULL;
            expr_node *m = new_node(p, EX_MEMBER, start);
            m->member.object   = node;
            m->member.pname    = name;
            m->member.optional = false;
            node = m;
        } else {
            break;
        }
    }
    return node;
}

/* ============================================================ */
/* primary                                                       */
/* ============================================================ */

static expr_node *parse_primary(parser_t *p)
{
    skip_ws(p);
    size_t start = p->pos;

    if (p->pos >= p->len) {
        parse_error(p, "unexpected end of expression");
        return NULL;
    }

    char ch = p->src[p->pos];

    /* Parenthesized */
    if (ch == '(') {
        p->pos++;
        expr_node *inner = parse_expr(p);
        if (!inner) return NULL;
        skip_ws(p);
        if (!expect_str(p, ")", 1)) return NULL;
        return inner;
    }

    if (ch == '{') return parse_object_literal(p, start);
    if (ch == '[') return parse_array_literal(p, start);
    if (ch == '\'' || ch == '"') return read_string_literal(p, start);

    if (ch == '-' && p->pos + 1 < p->len &&
        is_digit((unsigned char)p->src[p->pos + 1]))
        return read_number_literal(p, start);
    if (is_digit((unsigned char)ch))
        return read_number_literal(p, start);

    /* $ */
    if (ch == '$') {
        p->pos++;
        if (p->pos >= p->len || p->src[p->pos] != '.') {
            parse_error(p, "\"$\" must be followed by \".<identifier>\"");
            return NULL;
        }
        return new_node(p, EX_DOLLAR, start);
    }

    /* @name */
    if (ch == '@') {
        p->pos++;
        char *name = read_identifier(p);
        if (!name) {
            parse_error(p, "\"@\" must be followed by an identifier");
            return NULL;
        }
        expr_node *node = new_node(p, EX_AT, start);
        node->at.name = name;
        return node;
    }

    /* .name */
    if (ch == '.') {
        unsigned char nc = (p->pos + 1 < p->len)
                         ? (unsigned char)p->src[p->pos + 1] : 0;
        if (!is_id_start(nc)) {
            parse_error(p, "\".\" must be followed by an identifier");
            return NULL;
        }
        p->pos++;
        char *name = read_identifier(p);
        if (!name) return NULL;
        expr_node *node = new_node(p, EX_DOT, start);
        node->dot.name = name;
        return node;
    }

    /* Identifier — literal keyword or helper call */
    if (is_id_start((unsigned char)ch)) {
        char *name = read_identifier(p);
        if (!name) return NULL;

        if (strcmp(name, "true") == 0) {
            expr_node *n = new_node(p, EX_LITERAL, start);
            n->literal = rv_bool(1);
            return n;
        }
        if (strcmp(name, "false") == 0) {
            expr_node *n = new_node(p, EX_LITERAL, start);
            n->literal = rv_bool(0);
            return n;
        }
        if (strcmp(name, "null") == 0) {
            expr_node *n = new_node(p, EX_LITERAL, start);
            n->literal = rv_null();
            return n;
        }

        /* Must be a function call */
        skip_ws(p);
        if (p->pos >= p->len || p->src[p->pos] != '(') {
            parse_errorf(p,
                "bare identifier \"%s\" is not a valid expression "
                "(helpers must be called: \"%s(...)\")", name, name);
            return NULL;
        }
        p->pos++;

        nodevec_t args;
        nodevec_init(&args, p);

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] != ')') {
            while (true) {
                expr_node *a = parse_expr(p);
                if (!a) return NULL;
                nodevec_push(&args, p, a);
                skip_ws(p);
                if (p->pos < p->len && p->src[p->pos] == ',') {
                    p->pos++;
                    continue;
                }
                break;
            }
        }
        if (!expect_str(p, ")", 1)) return NULL;

        expr_node *node = new_node(p, EX_CALL, start);
        node->call.callee  = name;
        node->call.args    = args.items;
        node->call.n_args  = args.len;
        return node;
    }

    parse_errorf(p, "unexpected character \"%c\"", ch);
    return NULL;
}

/* ============================================================ */
/* Object literal                                                */
/* ============================================================ */

static int parse_object_key(parser_t *p, obj_entry *entry);
static expr_node *parse_computed_key_expr(parser_t *p);

static expr_node *parse_object_literal(parser_t *p, size_t start)
{
    p->pos++;
    entryvec_t entries;
    entryvec_init(&entries, p);

    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] != '}') {
        while (true) {
            obj_entry entry;
            memset(&entry, 0, sizeof(entry));

            if (parse_object_key(p, &entry) != 0) return NULL;

            skip_ws(p);
            if (!expect_str(p, ":", 1)) return NULL;

            entry.val = parse_expr(p);
            if (!entry.val) return NULL;

            entryvec_push(&entries, p, &entry);

            skip_ws(p);
            if (p->pos < p->len && p->src[p->pos] == ',') {
                p->pos++;
                skip_ws(p);
                if (p->pos < p->len && p->src[p->pos] == '}') break;
                continue;
            }
            break;
        }
    }

    skip_ws(p);
    if (!expect_str(p, "}", 1)) return NULL;

    expr_node *node = new_node(p, EX_OBJECT, start);
    node->object.entries = entries.entries;
    node->object.n       = entries.len;
    return node;
}

/* ============================================================ */
/* Array literal                                                 */
/* ============================================================ */

static expr_node *parse_array_literal(parser_t *p, size_t start)
{
    p->pos++;
    nodevec_t items;
    nodevec_init(&items, p);

    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] != ']') {
        while (true) {
            expr_node *item = parse_expr(p);
            if (!item) return NULL;
            nodevec_push(&items, p, item);
            skip_ws(p);
            if (p->pos < p->len && p->src[p->pos] == ',') {
                p->pos++;
                skip_ws(p);
                if (p->pos < p->len && p->src[p->pos] == ']') break;
                continue;
            }
            break;
        }
    }

    skip_ws(p);
    if (!expect_str(p, "]", 1)) return NULL;

    expr_node *node = new_node(p, EX_ARRAY, start);
    node->array.items = items.items;
    node->array.n     = items.len;
    return node;
}

/* ============================================================ */
/* Object key parsing                                            */
/* ============================================================ */

static int parse_object_key(parser_t *p, obj_entry *entry)
{
    skip_ws(p);
    char ch = (p->pos < p->len) ? p->src[p->pos] : '\0';

    /* Computed key */
    if (ch == '[') {
        p->pos++;
        skip_ws(p);
        expr_node *expr = parse_computed_key_expr(p);
        if (!expr) return -1;
        skip_ws(p);
        if (!expect_str(p, "]", 1)) return -1;
        entry->computed = true;
        entry->key_expr = expr;
        return 0;
    }

    /* String literal key */
    if (ch == '\'' || ch == '"') {
        expr_node *lit = read_string_literal(p, p->pos);
        if (!lit) return -1;
        entry->key  = (char *)lit->literal.string.data;
        entry->klen = lit->literal.string.len;
        entry->computed = false;
        return 0;
    }

    /* Number literal key */
    if (is_digit((unsigned char)ch) ||
        (ch == '-' && p->pos + 1 < p->len &&
         is_digit((unsigned char)p->src[p->pos + 1]))) {
        expr_node *lit = read_number_literal(p, p->pos);
        if (!lit) return -1;
        char numbuf[32];
        size_t nlen = number_to_string(lit->literal.number, numbuf);
        char *key = (char *)compile_arena_alloc(p->arena, p->L, nlen + 1);
        memcpy(key, numbuf, nlen);
        key[nlen] = '\0';
        entry->key  = key;
        entry->klen = nlen;
        entry->computed = false;
        return 0;
    }

    /* Identifier key */
    if (is_id_start((unsigned char)ch)) {
        char *name = read_identifier(p);
        if (!name) return -1;
        entry->key  = name;
        entry->klen = strlen(name);
        entry->computed = false;
        return 0;
    }

    parse_error(p, "expected object key (identifier, string, number, or [<expr>])");
    return -1;
}

/* ============================================================ */
/* Computed key expression                                       */
/* ============================================================ */

static expr_node *parse_computed_key_expr(parser_t *p)
{
    skip_ws(p);
    size_t start = p->pos;

    if (p->pos >= p->len) {
        parse_error(p, "unexpected end of computed key");
        return NULL;
    }

    char ch = p->src[p->pos];

    if (ch == '\'' || ch == '"')
        return read_string_literal(p, start);

    if (is_digit((unsigned char)ch) ||
        (ch == '-' && p->pos + 1 < p->len &&
         is_digit((unsigned char)p->src[p->pos + 1])))
        return read_number_literal(p, start);

    if (ch == '$' || ch == '@' || ch == '.') {
        expr_node *node;

        if (ch == '$') {
            p->pos++;
            if (p->pos >= p->len || p->src[p->pos] != '.') {
                parse_error(p, "\"$\" must be followed by \".<identifier>\"");
                return NULL;
            }
            node = new_node(p, EX_DOLLAR, start);
        } else if (ch == '@') {
            p->pos++;
            char *name = read_identifier(p);
            if (!name) {
                parse_error(p, "\"@\" must be followed by an identifier");
                return NULL;
            }
            node = new_node(p, EX_AT, start);
            node->at.name = name;
        } else {
            unsigned char nc = (p->pos + 1 < p->len)
                             ? (unsigned char)p->src[p->pos + 1] : 0;
            if (!is_id_start(nc)) {
                parse_error(p, "\".\" must be followed by an identifier");
                return NULL;
            }
            p->pos++;
            char *name = read_identifier(p);
            if (!name) return NULL;
            node = new_node(p, EX_DOT, start);
            node->dot.name = name;
        }

        /* postfix chain */
        while (true) {
            skip_ws(p);
            if (starts_with(p, "?.", 2)) {
                p->pos += 2;
                char *name = read_identifier(p);
                if (!name) {
                    parse_error(p, "expected identifier after \"?.\"");
                    return NULL;
                }
                expr_node *m = new_node(p, EX_MEMBER, start);
                m->member.object   = node;
                m->member.pname    = name;
                m->member.optional = true;
                node = m;
            } else if (starts_with(p, ".", 1)) {
                unsigned char nc = (p->pos + 1 < p->len)
                                 ? (unsigned char)p->src[p->pos + 1] : 0;
                if (!is_id_start(nc)) break;
                p->pos++;
                char *name = read_identifier(p);
                if (!name) return NULL;
                expr_node *m = new_node(p, EX_MEMBER, start);
                m->member.object   = node;
                m->member.pname    = name;
                m->member.optional = false;
                node = m;
            } else {
                break;
            }
        }
        return node;
    }

    parse_error(p, "computed object key must be a string, number, or scope "
                   "reference (helpers and operators are not allowed here)");
    return NULL;
}

/* ============================================================ */
/* Literals                                                      */
/* ============================================================ */

static expr_node *read_string_literal(parser_t *p, size_t start)
{
    char quote = p->src[p->pos];
    p->pos++;

    /* Pass 1: scan to find decoded length and validate */
    size_t decoded_len = 0;
    size_t scan = p->pos;
    while (scan < p->len) {
        unsigned char ch = (unsigned char)p->src[scan];
        if (ch == (unsigned char)quote) break;
        if (ch == 0x5c) {
            scan++;
            if (scan >= p->len) {
                parse_error(p, "unterminated string literal");
                return NULL;
            }
            char esc = p->src[scan];
            switch (esc) {
                case 'n': case 't': case 'r': case '\\':
                case '\'': case '"': case '`': case '0':
                    break;
                default:
                    parse_errorf(p, "invalid escape sequence \"\\%c\"", esc);
                    return NULL;
            }
            scan++;
            decoded_len++;
        } else {
            scan++;
            decoded_len++;
        }
    }

    if (scan >= p->len) {
        parse_error(p, "unterminated string literal");
        return NULL;
    }

    /* Pass 2: decode into arena-allocated buffer */
    char *data = (char *)compile_arena_alloc(p->arena, p->L, decoded_len + 1);
    size_t di = 0;
    while (p->pos < scan) {
        unsigned char ch = (unsigned char)p->src[p->pos];
        if (ch == 0x5c) {
            p->pos++;
            char esc = p->src[p->pos];
            p->pos++;
            switch (esc) {
                case 'n':  data[di++] = '\n'; break;
                case 't':  data[di++] = '\t'; break;
                case 'r':  data[di++] = '\r'; break;
                case '\\': data[di++] = '\\'; break;
                case '\'': data[di++] = '\''; break;
                case '"':  data[di++] = '"';  break;
                case '`':  data[di++] = '`';  break;
                case '0':  data[di++] = '\0'; break;
            }
        } else {
            data[di++] = (char)ch;
            p->pos++;
        }
    }
    data[di] = '\0';
    p->pos++; /* consume closing quote */

    expr_node *node = new_node(p, EX_LITERAL, start);
    node->literal.tag         = RV_STRING;
    node->literal.string.data = data;
    node->literal.string.len  = decoded_len;
    return node;
}

static expr_node *read_number_literal(parser_t *p, size_t start)
{
    size_t begin = p->pos;

    if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;
    while (p->pos < p->len && is_digit((unsigned char)p->src[p->pos])) p->pos++;
    if (p->pos < p->len && p->src[p->pos] == '.' &&
        p->pos + 1 < p->len &&
        is_digit((unsigned char)p->src[p->pos + 1])) {
        p->pos++;
        while (p->pos < p->len && is_digit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->pos < p->len &&
        (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-'))
            p->pos++;
        while (p->pos < p->len && is_digit((unsigned char)p->src[p->pos])) p->pos++;
    }

    size_t textlen = p->pos - begin;
    char stackbuf[64];
    char *buf = stackbuf;
    if (textlen >= sizeof(stackbuf)) {
        buf = (char *)compile_arena_alloc(p->arena, p->L, textlen + 1);
    }
    memcpy(buf, p->src + begin, textlen);
    buf[textlen] = '\0';

    char *endptr;
    double value = strtod(buf, &endptr);
    if (endptr != buf + textlen || isnan(value)) {
        parse_errorf(p, "invalid number literal \"%s\"", buf);
        return NULL;
    }

    expr_node *node = new_node(p, EX_LITERAL, start);
    node->literal = rv_number(value);
    return node;
}

static char *read_identifier(parser_t *p)
{
    if (p->pos >= p->len || !is_id_start((unsigned char)p->src[p->pos]))
        return NULL;
    size_t begin = p->pos;
    p->pos++;
    while (p->pos < p->len && is_id_cont((unsigned char)p->src[p->pos]))
        p->pos++;

    size_t len = p->pos - begin;
    char *name = (char *)compile_arena_alloc(p->arena, p->L, len + 1);
    memcpy(name, p->src + begin, len);
    name[len] = '\0';
    return name;
}

/* ============================================================ */
/* Public API                                                    */
/* ============================================================ */

expr_node *expr_parse(compile_arena *arena, lua_State *L,
                      const char *src, size_t len, reflow_error *err)
{
    if (!src || len == 0) {
        if (err) {
            err->type    = "ReflowCompileError";
            err->message = "expression parse error: empty expression";
        }
        return NULL;
    }

    parser_t p;
    p.src       = src;
    p.len       = len;
    p.pos       = 0;
    p.err       = err;
    p.has_error = false;
    p.arena     = arena;
    p.L         = L;

    expr_node *expr = parse_expr(&p);
    if (!expr) return NULL;

    skip_ws(&p);
    if (p.pos < p.len) {
        parse_error(&p, "unexpected token");
        return NULL;
    }

    return expr;
}
