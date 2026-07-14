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

#ifndef REFLOW_SELECTOR_PARSE_H
#define REFLOW_SELECTOR_PARSE_H

#include <lua.h>
#include <stdbool.h>
#include <stddef.h>
#include "../compile_arena.h"
#include "../error.h"

/*
 * CSS selector AST — a subset of CSS Level 3 chosen to fit server-side
 * fragment extraction. Every rejected construct raises a
 * ReflowSelectorError so callers see the mismatch at parse time.
 *
 * Grammar (see JS selector/parse.js for the reference):
 *
 *   SelectorList := Complex ( ',' Complex )*
 *   Complex      := Compound ( Combinator Compound )*
 *   Combinator   := ' '  |  '>'
 *   Compound     := ( Tag | '*' )? ( '#' Ident | '.' Ident | '[' Attr ']' | Pseudo )*
 *   Attr         := Ident ( ( '=' | '~=' | '|=' | '^=' | '$=' | '*=' ) ( Ident | String ) )?
 *   Pseudo       := ':' Ident ( '(' Integer ')' )?
 */

typedef enum {
    SEL_ATTR_OP_NONE = 0,   /* [name] */
    SEL_ATTR_OP_EQ,         /* [name=value] */
    SEL_ATTR_OP_TILDE,      /* [name~=value] */
    SEL_ATTR_OP_PIPE,       /* [name|=value] */
    SEL_ATTR_OP_CARET,      /* [name^=value] */
    SEL_ATTR_OP_DOLLAR,     /* [name$=value] */
    SEL_ATTR_OP_STAR,       /* [name*=value] */
} sel_attr_op;

typedef struct {
    const char *name;
    size_t      name_len;
    sel_attr_op op;
    const char *value;      /* NULL when op == NONE */
    size_t      value_len;
} sel_attr_cond;

typedef enum {
    SEL_PSEUDO_FIRST_CHILD = 0,
    SEL_PSEUDO_LAST_CHILD,
    SEL_PSEUDO_ONLY_CHILD,
    SEL_PSEUDO_FIRST_OF_TYPE,
    SEL_PSEUDO_LAST_OF_TYPE,
    SEL_PSEUDO_ONLY_OF_TYPE,
    SEL_PSEUDO_NTH_CHILD,          /* takes integer argument */
    SEL_PSEUDO_NTH_LAST_CHILD,
    SEL_PSEUDO_NTH_OF_TYPE,
    SEL_PSEUDO_NTH_LAST_OF_TYPE,
} sel_pseudo_name;

typedef struct {
    sel_pseudo_name name;
    long            n;      /* value of the integer argument for nth-*;
                             * unused (0) for the boolean pseudos. */
} sel_pseudo_cond;

typedef struct {
    const char      *tag;      /* NULL == universal '*' */
    size_t           tag_len;
    const char      *id;       /* NULL when no #id */
    size_t           id_len;
    const char     **classes;  /* pointers into arena */
    size_t          *class_lens;
    size_t           n_classes;
    sel_attr_cond   *attrs;
    size_t           n_attrs;
    sel_pseudo_cond *pseudos;
    size_t           n_pseudos;
} sel_compound;

typedef enum {
    SEL_COMB_NONE = 0,     /* first compound in the chain */
    SEL_COMB_DESCENDANT,   /* ' ' */
    SEL_COMB_CHILD,        /* '>' */
} sel_combinator;

typedef struct {
    sel_combinator combinator;
    sel_compound   compound;
} sel_complex_part;

typedef struct {
    sel_complex_part *parts;
    size_t            n_parts;
} sel_complex;

typedef struct {
    const char  *source;
    size_t       source_len;
    sel_complex *selectors;
    size_t       n_selectors;
    bool         has_positional;   /* true if any compound uses a pseudo */
} sel_compiled;

/*
 * Parse `source` into a sel_compiled tree. All memory is bump-allocated
 * from `arena`. On error returns NULL and populates err with type set to
 * "ReflowSelectorError" and a reason field of "syntax" or "unsupported".
 */
sel_compiled *selector_parse(compile_arena *arena, lua_State *L,
                             const char *source, size_t source_len,
                             reflow_error *err);

#endif /* REFLOW_SELECTOR_PARSE_H */
