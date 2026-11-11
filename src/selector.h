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

#ifndef REFLOW_SELECTOR_H
#define REFLOW_SELECTOR_H

#include <stddef.h>

#include <lauxlib.h>
#include <lua.h>

/* The metatable of a compiled selector, a reflow.selector userdata. */
#define REFLOW_SELECTOR_MT "reflow.selector"

/* A length-delimited byte string owned by the selector. data is NULL when
 * the string is absent. */
typedef struct selector_string_t {
    const char *data;
    size_t len;
} selector_string_t;

typedef enum selector_attr_op_t {
    SELECTOR_ATTR_EXISTS = 0, /* [a]   */
    SELECTOR_ATTR_EQUAL,      /* [a=v] */
    SELECTOR_ATTR_INCLUDES,   /* [a~=v] */
    SELECTOR_ATTR_DASH,       /* [a|=v] */
    SELECTOR_ATTR_PREFIX,     /* [a^=v] */
    SELECTOR_ATTR_SUFFIX,     /* [a$=v] */
    SELECTOR_ATTR_SUBSTRING   /* [a*=v] */
} selector_attr_op_t;

/* An attribute condition. value is absent for SELECTOR_ATTR_EXISTS and holds
 * the unescaped value otherwise. */
typedef struct selector_attr_t {
    selector_string_t name;
    selector_attr_op_t op;
    selector_string_t value;
} selector_attr_t;

typedef enum selector_pseudo_kind_t {
    SELECTOR_PSEUDO_FIRST_CHILD = 0,
    SELECTOR_PSEUDO_LAST_CHILD,
    SELECTOR_PSEUDO_ONLY_CHILD,
    SELECTOR_PSEUDO_FIRST_OF_TYPE,
    SELECTOR_PSEUDO_LAST_OF_TYPE,
    SELECTOR_PSEUDO_ONLY_OF_TYPE,
    SELECTOR_PSEUDO_NTH_CHILD,
    SELECTOR_PSEUDO_NTH_LAST_CHILD,
    SELECTOR_PSEUDO_NTH_OF_TYPE,
    SELECTOR_PSEUDO_NTH_LAST_OF_TYPE
} selector_pseudo_kind_t;

/* Return the name of a positional pseudo-class, as written in a selector. */
static inline const char *selector_pseudo_name(selector_pseudo_kind_t kind)
{
    static const char *const names[] = {
        "first-child",  "last-child",       "only-child", "first-of-type",
        "last-of-type", "only-of-type",     "nth-child",  "nth-last-child",
        "nth-of-type",  "nth-last-of-type",
    };

    return names[kind];
}

/* A positional pseudo-class. n is the positive integer argument of the
 * nth-* kinds and 0 for the others. */
typedef struct selector_pseudo_t {
    selector_pseudo_kind_t kind;
    double n;
} selector_pseudo_t;

/* The conditions of one compound selector, all of which must hold. tag is
 * absent for the universal selector or when no type is given, and is lower
 * case otherwise. The conditions keep their source order. */
typedef struct selector_compound_t {
    selector_string_t tag;
    selector_string_t id;
    const selector_string_t *classes;
    size_t nclasses;
    const selector_attr_t *attrs;
    size_t nattrs;
    const selector_pseudo_t *pseudos;
    size_t npseudos;
} selector_compound_t;

typedef enum selector_combinator_t {
    SELECTOR_COMBINATOR_NONE = 0, /* the leftmost part */
    SELECTOR_COMBINATOR_DESCENDANT,
    SELECTOR_COMBINATOR_CHILD
} selector_combinator_t;

/* One part of a complex selector: the combinator joining it to the part on
 * its left, and its compound selector. */
typedef struct selector_part_t {
    selector_combinator_t combinator;
    selector_compound_t compound;
} selector_part_t;

/* A complex selector. The parts are in source order, so matching starts
 * from parts[nparts - 1] and walks toward parts[0]. */
typedef struct selector_complex_t {
    const selector_part_t *parts;
    size_t nparts;
} selector_complex_t;

/* A compiled selector. It is immutable once created and owns its source and
 * every condition, so it can be shared by any number of renders. */
typedef struct selector_t {
    selector_string_t source;
    int has_positional;
    const selector_complex_t *complexes;
    size_t ncomplexes;
} selector_t;

/* Return the compiled selector at idx, or raise a Lua error when the value
 * is not one. */
static inline const selector_t *selector_check(lua_State *L, int idx)
{
    return (const selector_t *)luaL_checkudata(L, idx, REFLOW_SELECTOR_MT);
}

#endif /* REFLOW_SELECTOR_H */
