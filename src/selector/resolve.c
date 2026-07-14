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
#include "resolve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "match.h"

/* Persistent error scratch — resolve is single-threaded like the rest
 * of the C core. */
static char g_resolve_err[512];
static char g_resolve_feature[128];

/* --- Seed selection --------------------------------------------------- */

/*
 * Return the seed candidate list for the rightmost compound of the
 * complex selector. Anchors are tried in order of expected selectivity:
 * id > class > tag > attribute-name. When none applies (bare `*` or
 * bare positional pseudo) we fall back to the flat `all` list.
 */
static const ir_node **seed_for_complex(const sel_index *index,
                                        const sel_complex *complex,
                                        size_t *out_n)
{
    const sel_compound *last =
        &complex->parts[complex->n_parts - 1].compound;

    if (last->id != NULL) {
        return sel_bucket_lookup(&index->by_id, last->id, last->id_len,
                                 out_n);
    }
    if (last->n_classes > 0) {
        /* Pick the class with the smallest bucket to keep the downstream
         * verification cheap. Absent classes short-circuit the whole
         * result set. */
        const ir_node **best = NULL;
        size_t best_n = 0;
        for (size_t i = 0; i < last->n_classes; i++) {
            size_t n = 0;
            const ir_node **b = sel_bucket_lookup(
                &index->by_class, last->classes[i], last->class_lens[i],
                &n);
            if (b == NULL) { *out_n = 0; return NULL; }
            if (best == NULL || n < best_n) {
                best   = b;
                best_n = n;
            }
        }
        *out_n = best_n;
        return best;
    }
    if (last->tag != NULL) {
        return sel_bucket_lookup(&index->by_tag, last->tag, last->tag_len,
                                 out_n);
    }
    if (last->n_attrs > 0) {
        const ir_node **best = NULL;
        size_t best_n = 0;
        for (size_t i = 0; i < last->n_attrs; i++) {
            size_t n = 0;
            const ir_node **b = sel_bucket_lookup(
                &index->by_attr_name,
                last->attrs[i].name, last->attrs[i].name_len, &n);
            if (b == NULL) { *out_n = 0; return NULL; }
            if (best == NULL || n < best_n) {
                best   = b;
                best_n = n;
            }
        }
        *out_n = best_n;
        return best;
    }
    /* Bare universal / bare positional pseudo. */
    *out_n = index->n_all;
    return index->all;
}

/* --- Complex matching ------------------------------------------------- */

/*
 * With `el` already confirmed for the rightmost compound, walk up the
 * ancestor chain to satisfy preceding parts. Combinator info lives on
 * the RIGHT side of each part (matching the parser output), so at
 * position pi we consume `parts[pi + 1].combinator` before trying to
 * match `parts[pi].compound`.
 */
static bool matches_complex(const ir_node *el, const sel_complex *complex)
{
    if (!sel_match_compound(el,
        &complex->parts[complex->n_parts - 1].compound)) return false;
    if (complex->n_parts == 1) return true;

    const ir_node *cur = el->element.parent;
    for (long pi = (long)complex->n_parts - 2; pi >= 0; pi--) {
        sel_combinator combinator = complex->parts[pi + 1].combinator;
        const sel_compound *compound = &complex->parts[pi].compound;

        if (combinator == SEL_COMB_CHILD) {
            if (cur == NULL || !sel_match_compound(cur, compound)) {
                return false;
            }
            cur = cur->element.parent;
            continue;
        }
        /* Descendant combinator: any ancestor may satisfy this part. */
        const ir_node *found = NULL;
        while (cur != NULL) {
            if (sel_match_compound(cur, compound)) {
                found = cur;
                break;
            }
            cur = cur->element.parent;
        }
        if (found == NULL) return false;
        cur = found->element.parent;
    }
    return true;
}

/* --- Positional collection ------------------------------------------- */

static const char *pseudo_name_str(sel_pseudo_name n)
{
    static const char *names[] = {
        "first-child", "last-child", "only-child",
        "first-of-type", "last-of-type", "only-of-type",
        "nth-child", "nth-last-child",
        "nth-of-type", "nth-last-of-type",
    };
    return (n >= 0 && (int)n < 10) ? names[n] : "?";
}

/*
 * Positional pseudos on non-terminal compounds are rejected fail-fast:
 * evaluating them requires runtime sibling counts of ANCESTOR
 * emissions, which is outside the fragment-fetching model. Compound
 * position N-1 always carries the terminal pseudos.
 */
static int reject_ancestor_positional(const sel_complex *complex,
                                      reflow_error *err)
{
    for (size_t i = 0; i + 1 < complex->n_parts; i++) {
        const sel_compound *c = &complex->parts[i].compound;
        if (c->n_pseudos == 0) continue;
        const char *name = pseudo_name_str(c->pseudos[0].name);
        snprintf(g_resolve_err, sizeof(g_resolve_err),
                 "positional pseudo-class \":%s\" is only supported on "
                 "the rightmost compound selector",
                 name);
        snprintf(g_resolve_feature, sizeof(g_resolve_feature),
                 "pseudo-ancestor:%s", name);
        err->type    = "ReflowSelectorError";
        err->message = g_resolve_err;
        err->reason  = "unsupported";
        err->feature = g_resolve_feature;
        return -1;
    }
    return 0;
}

/* --- Public entry point ---------------------------------------------- */

/*
 * Grow-friendly merged result store, keyed by element pointer. The
 * expected result size is small (fragment lookups return one element in
 * the common case), so a flat linear array with pointer equality is
 * enough — no hash needed.
 */
typedef struct {
    sel_candidate *items;
    size_t         count;
    size_t         cap;
} merge_buf_t;

static int merge_push(merge_buf_t *m, const ir_node *el,
                      const sel_pseudo_cond *pseudos, size_t n_pseudos)
{
    for (size_t i = 0; i < m->count; i++) {
        if (m->items[i].element == el) return 0;   /* dedup */
    }
    if (m->count == m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 4;
        sel_candidate *nb =
            (sel_candidate *)realloc(m->items, ncap * sizeof(*nb));
        if (!nb) return -1;
        m->items = nb;
        m->cap   = ncap;
    }
    m->items[m->count].element      = el;
    m->items[m->count].positional   = pseudos;
    m->items[m->count].n_positional = n_pseudos;
    m->count++;
    return 0;
}

static int cmp_by_order(const void *a, const void *b)
{
    size_t oa = ((const sel_candidate *)a)->element->element.order;
    size_t ob = ((const sel_candidate *)b)->element->element.order;
    if (oa < ob) return -1;
    if (oa > ob) return 1;
    return 0;
}

sel_candidates *sel_resolve(compile_arena *arena, lua_State *L,
                            const sel_index *index,
                            const sel_compiled *selector,
                            reflow_error *err)
{
    /* Reject positional pseudos on ancestor compounds up-front so we
     * never do wasted matching work when the selector is unsupported. */
    for (size_t si = 0; si < selector->n_selectors; si++) {
        if (reject_ancestor_positional(&selector->selectors[si], err) != 0) {
            return NULL;
        }
    }

    merge_buf_t buf = {0};

    for (size_t si = 0; si < selector->n_selectors; si++) {
        const sel_complex *complex = &selector->selectors[si];
        size_t             n_seeds = 0;
        const ir_node    **seeds   =
            seed_for_complex(index, complex, &n_seeds);
        if (seeds == NULL || n_seeds == 0) continue;

        const sel_compound *last =
            &complex->parts[complex->n_parts - 1].compound;

        for (size_t k = 0; k < n_seeds; k++) {
            const ir_node *el = seeds[k];
            if (!matches_complex(el, complex)) continue;
            if (merge_push(&buf, el, last->pseudos,
                           last->n_pseudos) != 0) {
                free(buf.items);
                err->type    = "ReflowSelectorError";
                err->message = "selector resolve: out of memory";
                err->reason  = "syntax";
                return NULL;
            }
        }
    }

    /* Copy the merged set into arena storage in document order. */
    sel_candidates *out =
        (sel_candidates *)compile_arena_alloc(arena, L, sizeof(*out));
    out->count = buf.count;
    out->items = (sel_candidate *)compile_arena_alloc(
        arena, L, sizeof(sel_candidate) * (buf.count == 0 ? 1 : buf.count));
    if (buf.count > 0) {
        qsort(buf.items, buf.count, sizeof(sel_candidate), cmp_by_order);
        memcpy(out->items, buf.items,
               buf.count * sizeof(sel_candidate));
    }
    free(buf.items);
    return out;
}

/* --- Runtime positional evaluation ----------------------------------- */

bool sel_eval_positional(const sel_pseudo_cond *cond,
                         size_t idx, size_t total,
                         size_t of_type_index, size_t of_type_total)
{
    switch (cond->name) {
    case SEL_PSEUDO_FIRST_CHILD:      return idx == 1;
    case SEL_PSEUDO_LAST_CHILD:       return idx == total;
    case SEL_PSEUDO_ONLY_CHILD:       return total == 1;
    case SEL_PSEUDO_FIRST_OF_TYPE:    return of_type_index == 1;
    case SEL_PSEUDO_LAST_OF_TYPE:     return of_type_index == of_type_total;
    case SEL_PSEUDO_ONLY_OF_TYPE:     return of_type_total == 1;
    case SEL_PSEUDO_NTH_CHILD:        return (long)idx == cond->n;
    case SEL_PSEUDO_NTH_LAST_CHILD:
        return (long)total - cond->n + 1 == (long)idx;
    case SEL_PSEUDO_NTH_OF_TYPE:      return (long)of_type_index == cond->n;
    case SEL_PSEUDO_NTH_LAST_OF_TYPE:
        return (long)of_type_total - cond->n + 1 == (long)of_type_index;
    default:                          return false;
    }
}
