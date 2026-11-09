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

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "selector_compile.h"

#define NOT_SUPPORTED_POSITIONAL                                               \
    " is not supported (supported: :first-child, :last-child, :only-child, "   \
    ":first-of-type, :last-of-type, :only-of-type, :nth-child(n), "            \
    ":nth-last-child(n), :nth-of-type(n), :nth-last-of-type(n))"

/* A growable array in the pool. */
typedef struct {
    void *items;
    size_t count;
    size_t capacity;
} array_t;

typedef struct {
    lua_State *L;
    pool_t *pool;
    const char *s;
    size_t len;
    size_t pos;
    selector_parse_error_t *err;
} parser_t;

static const struct {
    const char *name;
    selector_pseudo_kind_t kind;
    int nth;
} PSEUDOS[] = {
    {"first-child",      SELECTOR_PSEUDO_FIRST_CHILD,      0},
    {"last-child",       SELECTOR_PSEUDO_LAST_CHILD,       0},
    {"only-child",       SELECTOR_PSEUDO_ONLY_CHILD,       0},
    {"first-of-type",    SELECTOR_PSEUDO_FIRST_OF_TYPE,    0},
    {"last-of-type",     SELECTOR_PSEUDO_LAST_OF_TYPE,     0},
    {"only-of-type",     SELECTOR_PSEUDO_ONLY_OF_TYPE,     0},
    {"nth-child",        SELECTOR_PSEUDO_NTH_CHILD,        1},
    {"nth-last-child",   SELECTOR_PSEUDO_NTH_LAST_CHILD,   1},
    {"nth-of-type",      SELECTOR_PSEUDO_NTH_OF_TYPE,      1},
    {"nth-last-of-type", SELECTOR_PSEUDO_NTH_LAST_OF_TYPE, 1},
};

/* Return the byte at the current position, or -1 at the end. */
static int peek(const parser_t *p)
{
    return p->pos < p->len ? (unsigned char)p->s[p->pos] : -1;
}

static int peek_at(const parser_t *p, size_t offset)
{
    return p->pos + offset < p->len ? (unsigned char)p->s[p->pos + offset] : -1;
}

/* The whitespace of parse.js: space, tab, LF, CR, and FF. */
static int is_ws(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '-';
}

static int is_ident_part(int c)
{
    return is_ident_start(c) || is_digit(c);
}

static void skip_ws(parser_t *p)
{
    while (is_ws(peek(p))) {
        p->pos++;
    }
}

/* Fail at pos with the message and the feature on top of the stack. */
static int fail(parser_t *p, int unsupported, size_t pos)
{
    p->err->unsupported = unsupported;
    p->err->pos         = pos;
    return 1;
}

/* Push "selector syntax error: <message>" and no feature. */
static int syntax_error(parser_t *p, size_t pos, const char *message)
{
    lua_pushfstring(p->L, "selector syntax error: %s", message);
    lua_pushnil(p->L);
    return fail(p, 0, pos);
}

/* The byte length of the UTF-8 character at s[pos], or 1 when it is not a
 * complete one, so that a message quotes a whole character. */
static size_t char_len(const char *s, size_t len, size_t pos)
{
    unsigned char c = (unsigned char)s[pos];
    size_t n        = c < 0x80              ? 1 :
                      c >= 0xf0 && c < 0xf5 ? 4 :
                      c >= 0xe0             ? 3 :
                      c >= 0xc2             ? 2 :
                                              1;
    size_t i        = 0;

    if (c >= 0xf5 || pos + n > len) {
        return 1;
    }
    for (i = 1; i < n; i++) {
        if (((unsigned char)s[pos + i] & 0xc0) != 0x80) {
            return 1;
        }
    }
    return n;
}

/* Make room for one more item of size bytes. */
static void *array_push(parser_t *p, array_t *array, size_t size)
{
    if (array->count == array->capacity) {
        size_t capacity = array->capacity ? array->capacity * 2 : 4;
        void *items     = NULL;

        if (capacity > SIZE_MAX / size) {
            errno = ENOMEM;
            return NULL;
        }
        items = pool_realloc(p->pool, array->items, capacity * size);
        if (items == NULL) {
            return NULL;
        }
        array->items    = items;
        array->capacity = capacity;
    }
    return (char *)array->items + size * array->count++;
}

/* Read an identifier; the current byte must start one. */
static selector_string_t read_ident(parser_t *p)
{
    size_t start = p->pos++;

    while (is_ident_part(peek(p))) {
        p->pos++;
    }
    return (selector_string_t){p->s + start, p->pos - start};
}

/* Return a lower-cased copy of s in the pool, or NULL when the pool runs
 * out of memory. Identifiers are ASCII. */
static char *lower_copy(parser_t *p, const selector_string_t *s)
{
    char *copy = (char *)pool_alloc(p->pool, s->len + 1);
    size_t i   = 0;

    if (copy == NULL) {
        return NULL;
    }
    for (i = 0; i < s->len; i++) {
        char c  = s->data[i];
        copy[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    copy[s->len] = '\0';
    return copy;
}

/* Read a quoted string after its opening quote, resolving each backslash to
 * the character after it. */
static int read_string(parser_t *p, int quote, selector_string_t *value)
{
    char *out  = (char *)pool_alloc(p->pool, p->len - p->pos + 1);
    size_t len = 0;

    if (out == NULL) {
        return -1;
    }
    p->pos++;
    while (p->pos < p->len) {
        int c = (unsigned char)p->s[p->pos++];

        if (c == quote) {
            out[len] = '\0';
            *value   = (selector_string_t){out, len};
            return 0;
        } else if (c == '\\') {
            if (p->pos >= p->len) {
                return syntax_error(p, p->pos, "unterminated string escape");
            }
            out[len++] = p->s[p->pos++];
        } else if (c == '\n' || c == '\r') {
            return syntax_error(p, p->pos - 1, "unterminated string literal");
        } else {
            out[len++] = (char)c;
        }
    }
    return syntax_error(p, p->pos, "unterminated string literal");
}

static int parse_attr(parser_t *p, array_t *attrs)
{
    lua_State *L          = p->L;
    selector_attr_t attr  = {0};
    selector_attr_t *slot = NULL;
    size_t attr_start     = 0;
    int c0                = 0;
    int rc                = 0;

    p->pos++; /* [ */
    skip_ws(p);
    if (!is_ident_start(peek(p))) {
        return syntax_error(p, p->pos, "expected attribute name");
    }
    attr_start = p->pos;
    attr.name  = read_ident(p);
    /* A "|" that is not part of "|=" or "||" separates a namespace. */
    if (peek(p) == '|' && peek_at(p, 1) != '=' && peek_at(p, 1) != '|') {
        lua_pushliteral(L, "selector unsupported: attribute namespaces are "
                           "not supported");
        lua_pushliteral(L, "attr-namespace");
        return fail(p, 1, attr_start);
    }
    skip_ws(p);

    if (peek(p) != ']') {
        static const char ops[]                    = "~|^$*";
        static const selector_attr_op_t op_kinds[] = {
            SELECTOR_ATTR_INCLUDES, SELECTOR_ATTR_DASH, SELECTOR_ATTR_PREFIX,
            SELECTOR_ATTR_SUFFIX, SELECTOR_ATTR_SUBSTRING};
        const char *op_name = NULL;
        int vc              = 0;

        c0 = peek(p);
        if (c0 == '=') {
            attr.op = SELECTOR_ATTR_EQUAL;
            op_name = "=";
            p->pos++;
        } else if (c0 > 0 && strchr(ops, c0) != NULL) {
            if (peek_at(p, 1) != '=') {
                lua_pushfstring(L,
                                "selector syntax error: expected \"=\" after "
                                "\"%c\" in attribute selector",
                                c0);
                lua_pushnil(L);
                return fail(p, 0, p->pos + 1);
            }
            attr.op = op_kinds[strchr(ops, c0) - ops];
            op_name = attr.op == SELECTOR_ATTR_INCLUDES ? "~=" :
                      attr.op == SELECTOR_ATTR_DASH     ? "|=" :
                      attr.op == SELECTOR_ATTR_PREFIX   ? "^=" :
                      attr.op == SELECTOR_ATTR_SUFFIX   ? "$=" :
                                                          "*=";
            p->pos += 2;
        } else {
            return syntax_error(p, p->pos,
                                "expected attribute operator or \"]\"");
        }
        skip_ws(p);
        vc = peek(p);
        if (vc == '"' || vc == '\'') {
            if ((rc = read_string(p, vc, &attr.value)) != 0) {
                return rc;
            }
        } else if (is_ident_start(vc)) {
            attr.value = read_ident(p);
        } else {
            lua_pushfstring(L,
                            "selector syntax error: expected attribute value "
                            "after \"%s\"",
                            op_name);
            lua_pushnil(L);
            return fail(p, 0, p->pos);
        }
        skip_ws(p);
        /* A case-sensitivity flag (i or s) is not supported. */
        if (peek(p) == 'i' || peek(p) == 'I' || peek(p) == 's' ||
            peek(p) == 'S') {
            lua_pushliteral(L, "selector unsupported: attribute "
                               "case-sensitivity flag is not supported");
            lua_pushliteral(L, "attr-case-flag");
            return fail(p, 1, p->pos);
        }
    }
    if (peek(p) != ']') {
        return syntax_error(p, p->pos,
                            "expected \"]\" to close attribute selector");
    }
    p->pos++;
    if ((slot = (selector_attr_t *)array_push(p, attrs, sizeof(*slot))) ==
        NULL) {
        return -1;
    }
    *slot = attr;
    return 0;
}

/* Push ":<name>" for a message. */
static void push_pseudo_name(lua_State *L, const selector_string_t *name)
{
    lua_pushliteral(L, ":");
    lua_pushlstring(L, name->data, name->len);
    lua_concat(L, 2);
}

static int parse_pseudo(parser_t *p, array_t *pseudos)
{
    lua_State *L             = p->L;
    selector_string_t raw    = {0};
    selector_string_t name   = {0};
    char *lowered            = NULL;
    selector_pseudo_t pseudo = {0};
    selector_pseudo_t *slot  = NULL;
    size_t name_start        = 0;
    size_t num_start         = 0;
    size_t i                 = 0;
    int found                = -1;

    p->pos++; /* : */
    if (!is_ident_start(peek(p))) {
        return syntax_error(p, p->pos,
                            "expected pseudo-class name after \":\"");
    }
    name_start = p->pos;
    raw        = read_ident(p);
    if ((lowered = lower_copy(p, &raw)) == NULL) {
        return -1;
    }
    name = (selector_string_t){lowered, raw.len};
    for (i = 0; i < sizeof(PSEUDOS) / sizeof(PSEUDOS[0]); i++) {
        if (strlen(PSEUDOS[i].name) == name.len &&
            memcmp(PSEUDOS[i].name, name.data, name.len) == 0) {
            found = (int)i;
            break;
        }
    }
    if (found < 0) {
        lua_pushliteral(L, "selector unsupported: pseudo-class \"");
        push_pseudo_name(L, &raw);
        lua_pushliteral(L, "\"" NOT_SUPPORTED_POSITIONAL);
        lua_concat(L, 3);
        lua_pushliteral(L, "pseudo:");
        lua_pushlstring(L, name.data, name.len);
        lua_concat(L, 2);
        return fail(p, 1, name_start - 1);
    }
    pseudo.kind = PSEUDOS[found].kind;

    if (!PSEUDOS[found].nth) {
        if (peek(p) == '(') {
            lua_pushliteral(L, "selector syntax error: \"");
            push_pseudo_name(L, &name);
            lua_pushliteral(L, "\" does not take an argument");
            lua_concat(L, 3);
            lua_pushnil(L);
            return fail(p, 0, p->pos);
        }
    } else {
        const char *digits = NULL;
        char *copy         = NULL;
        size_t ndigits     = 0;

        if (peek(p) != '(') {
            lua_pushliteral(L, "selector syntax error: \"");
            push_pseudo_name(L, &name);
            lua_pushliteral(L, "\" requires an integer argument, e.g. \"");
            push_pseudo_name(L, &name);
            lua_pushliteral(L, "(3)\"");
            lua_concat(L, 5);
            lua_pushnil(L);
            return fail(p, 0, p->pos);
        }
        p->pos++;
        skip_ws(p);
        num_start = p->pos;
        if (is_digit(peek(p))) {
            digits = p->s + p->pos;
            while (is_digit(peek(p))) {
                p->pos++;
                ndigits++;
            }
            skip_ws(p);
        }
        if (digits == NULL || peek(p) != ')') {
            /* Formulas such as An+B, odd, even, and negative numbers. */
            lua_pushliteral(L, "selector unsupported: \"");
            push_pseudo_name(L, &name);
            lua_pushliteral(L, "\" accepts only a positive integer literal; "
                               "formulas (An+B, odd, even) are not supported");
            lua_concat(L, 3);
            lua_pushliteral(L, "pseudo-arg:");
            lua_pushlstring(L, name.data, name.len);
            lua_concat(L, 2);
            return fail(p, 1, digits == NULL ? p->pos : num_start);
        }
        p->pos++;
        /* As Number(digits) in JavaScript: too many digits overflow. */
        if ((copy = (char *)pool_alloc(p->pool, ndigits + 1)) == NULL) {
            return -1;
        }
        memcpy(copy, digits, ndigits);
        copy[ndigits] = '\0';
        pseudo.n      = strtod(copy, NULL);
        pool_free(p->pool, copy);
        if (!isfinite(pseudo.n) || pseudo.n < 1) {
            lua_pushliteral(L, "selector syntax error: \"");
            push_pseudo_name(L, &name);
            lua_pushliteral(L, "(n)\" argument must be a positive integer "
                               "(>= 1)");
            lua_concat(L, 3);
            lua_pushnil(L);
            return fail(p, 0, num_start);
        }
    }
    pool_free(p->pool, lowered);
    if ((slot = (selector_pseudo_t *)array_push(p, pseudos, sizeof(*slot))) ==
        NULL) {
        return -1;
    }
    *slot = pseudo;
    return 0;
}

static int parse_compound(parser_t *p, selector_compound_t *compound)
{
    array_t classes = {0};
    array_t attrs   = {0};
    array_t pseudos = {0};
    size_t count    = 0;
    int rc          = 0;

    *compound = (selector_compound_t){0};
    if (peek(p) == '*') {
        /* The universal selector leaves the tag absent. */
        p->pos++;
        count++;
    } else if (is_ident_start(peek(p))) {
        selector_string_t tag = read_ident(p);
        char *lowered         = lower_copy(p, &tag);

        if (lowered == NULL) {
            return -1;
        }
        compound->tag = (selector_string_t){lowered, tag.len};
        count++;
    }

    while (p->pos < p->len) {
        int c = peek(p);

        if (c == '#' || c == '.') {
            selector_string_t *slot = NULL;

            p->pos++;
            if (!is_ident_start(peek(p))) {
                return syntax_error(p, p->pos,
                                    c == '#' ?
                                        "expected identifier after \"#\"" :
                                        "expected identifier after \".\"");
            } else if (c == '#') {
                if (compound->id.data != NULL) {
                    return syntax_error(p, p->pos,
                                        "multiple \"#id\" in the same "
                                        "compound selector");
                }
                compound->id = read_ident(p);
            } else if ((slot = (selector_string_t *)array_push(
                            p, &classes, sizeof(*slot))) == NULL) {
                return -1;
            } else {
                *slot = read_ident(p);
            }
        } else if (c == '[') {
            if ((rc = parse_attr(p, &attrs)) != 0) {
                return rc;
            }
        } else if (c == ':') {
            if (peek_at(p, 1) == ':') {
                lua_pushliteral(p->L, "selector unsupported: pseudo-elements "
                                      "(\"::\") are not supported");
                lua_pushliteral(p->L, "pseudo-element");
                return fail(p, 1, p->pos);
            } else if ((rc = parse_pseudo(p, &pseudos)) != 0) {
                return rc;
            }
        } else {
            break;
        }
        count++;
    }
    if (count == 0) {
        return syntax_error(p, p->pos, "expected selector");
    }
    compound->classes  = (const selector_string_t *)classes.items;
    compound->nclasses = classes.count;
    compound->attrs    = (const selector_attr_t *)attrs.items;
    compound->nattrs   = attrs.count;
    compound->pseudos  = (const selector_pseudo_t *)pseudos.items;
    compound->npseudos = pseudos.count;
    return 0;
}

static int parse_complex(parser_t *p, selector_complex_t *complex,
                         int *has_positional)
{
    lua_State *L                     = p->L;
    array_t parts                    = {0};
    selector_combinator_t combinator = SELECTOR_COMBINATOR_NONE;
    int rc                           = 0;

    skip_ws(p);
    for (;;) {
        selector_part_t *part =
            (selector_part_t *)array_push(p, &parts, sizeof(*part));
        size_t before = 0;
        int c         = 0;

        if (part == NULL) {
            return -1;
        } else if ((rc = parse_compound(p, &part->compound)) != 0) {
            return rc;
        }
        part->combinator = combinator;
        if (part->compound.npseudos > 0) {
            *has_positional = 1;
        }

        before = p->pos;
        skip_ws(p);
        c = peek(p);
        if (c < 0 || c == ',') {
            break;
        } else if (c == '>') {
            p->pos++;
            skip_ws(p);
            combinator = SELECTOR_COMBINATOR_CHILD;
        } else if (c == '+' || c == '~') {
            lua_pushfstring(L,
                            "selector unsupported: combinator \"%c\" is not "
                            "supported (only descendant \" \" and child "
                            "\">\" are supported)",
                            c);
            lua_pushfstring(L, "combinator:%c", c);
            return fail(p, 1, p->pos);
        } else if (c == '|' && peek_at(p, 1) == '|') {
            lua_pushliteral(L, "selector unsupported: column combinator "
                               "\"||\" is not supported");
            lua_pushliteral(L, "combinator:||");
            return fail(p, 1, p->pos);
        } else if (p->pos > before) {
            combinator = SELECTOR_COMBINATOR_DESCENDANT;
        } else {
            break;
        }
    }
    complex->parts  = (const selector_part_t *)parts.items;
    complex->nparts = parts.count;
    return 0;
}

int selector_parse(lua_State *L, pool_t *pool, selector_t *sel,
                   selector_parse_error_t *err)
{
    parser_t p = {
        .L    = L,
        .pool = pool,
        .s    = sel->source.data,
        .len  = sel->source.len,
        .err  = err,
    };
    array_t complexes  = {0};
    int has_positional = 0;
    int rc             = 0;

    do {
        selector_complex_t *complex = NULL;

        if (complexes.count > 0) {
            p.pos++; /* , */
            skip_ws(&p);
        }
        complex =
            (selector_complex_t *)array_push(&p, &complexes, sizeof(*complex));
        if (complex == NULL) {
            return -1;
        } else if ((rc = parse_complex(&p, complex, &has_positional)) != 0) {
            return rc;
        }
    } while (peek(&p) == ',');
    skip_ws(&p);
    if (p.pos < p.len) {
        lua_pushliteral(L, "selector syntax error: unexpected \"");
        lua_pushlstring(L, p.s + p.pos, char_len(p.s, p.len, p.pos));
        lua_pushliteral(L, "\"");
        lua_concat(L, 3);
        lua_pushnil(L);
        return fail(&p, 0, p.pos);
    }
    sel->has_positional = has_positional;
    sel->complexes      = (const selector_complex_t *)complexes.items;
    sel->ncomplexes     = complexes.count;
    return 0;
}
