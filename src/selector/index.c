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
#include "index.h"
#include <stdlib.h>
#include <string.h>

/* --- Bucket construction --------------------------------------------- */

typedef struct {
    const char     *key;
    size_t          key_len;
    const ir_node **items;
    size_t          count;
    size_t          cap;
} bucket_entry_t;

typedef struct {
    bucket_entry_t *entries;
    size_t          count;
    size_t          cap;
} bucket_builder_t;

static void bb_free(bucket_builder_t *bb)
{
    for (size_t i = 0; i < bb->count; i++) {
        free((void *)bb->entries[i].items);
    }
    free(bb->entries);
    bb->entries = NULL;
    bb->count   = 0;
    bb->cap     = 0;
}

static bucket_entry_t *bb_find_or_new(bucket_builder_t *bb,
                                      const char *key, size_t key_len)
{
    for (size_t i = 0; i < bb->count; i++) {
        if (bb->entries[i].key_len == key_len &&
            memcmp(bb->entries[i].key, key, key_len) == 0) {
            return &bb->entries[i];
        }
    }
    if (bb->count == bb->cap) {
        size_t ncap = bb->cap ? bb->cap * 2 : 8;
        bucket_entry_t *nb =
            (bucket_entry_t *)realloc(bb->entries, ncap * sizeof(*nb));
        if (!nb) return NULL;
        bb->entries = nb;
        bb->cap     = ncap;
    }
    bucket_entry_t *e = &bb->entries[bb->count++];
    e->key     = key;
    e->key_len = key_len;
    e->items   = NULL;
    e->count   = 0;
    e->cap     = 0;
    return e;
}

static int bb_push(bucket_builder_t *bb, const char *key, size_t key_len,
                   const ir_node *el)
{
    bucket_entry_t *e = bb_find_or_new(bb, key, key_len);
    if (!e) return -1;
    if (e->count == e->cap) {
        size_t ncap = e->cap ? e->cap * 2 : 4;
        const ir_node **nb =
            (const ir_node **)realloc((void *)e->items,
                                      ncap * sizeof(*nb));
        if (!nb) return -1;
        e->items = nb;
        e->cap   = ncap;
    }
    e->items[e->count++] = el;
    return 0;
}

/* Commit the builder into arena-owned sel_bucket storage. */
static void bb_commit(bucket_builder_t *bb, compile_arena *arena,
                      lua_State *L, sel_bucket *out)
{
    size_t total = 0;
    for (size_t i = 0; i < bb->count; i++) total += bb->entries[i].count;

    out->n_keys    = bb->count;
    out->n_entries = total;
    out->keys      = (sel_bucket_key *)compile_arena_alloc(
        arena, L, sizeof(sel_bucket_key) * (bb->count == 0 ? 1 : bb->count));
    out->entries = (const ir_node **)compile_arena_alloc(
        arena, L, sizeof(const ir_node *) * (total == 0 ? 1 : total));

    size_t off = 0;
    for (size_t i = 0; i < bb->count; i++) {
        bucket_entry_t *e = &bb->entries[i];
        /* Deep-copy the key so the bucket doesn't reference caller memory
         * (helper strings, source spans) whose lifetime differs from the
         * arena. All existing callers already pass arena-owned keys, but
         * copying here removes an easy-to-miss aliasing hazard. */
        char *kdup = (char *)compile_arena_alloc(arena, L, e->key_len + 1);
        memcpy(kdup, e->key, e->key_len);
        kdup[e->key_len]      = '\0';
        out->keys[i].key      = kdup;
        out->keys[i].key_len  = e->key_len;
        out->keys[i].offset   = off;
        out->keys[i].count    = e->count;
        memcpy(&out->entries[off], e->items,
               e->count * sizeof(const ir_node *));
        off += e->count;
    }
}

const ir_node **sel_bucket_lookup(const sel_bucket *b,
                                  const char *key, size_t key_len,
                                  size_t *n)
{
    for (size_t i = 0; i < b->n_keys; i++) {
        if (b->keys[i].key_len == key_len &&
            memcmp(b->keys[i].key, key, key_len) == 0) {
            *n = b->keys[i].count;
            return &b->entries[b->keys[i].offset];
        }
    }
    *n = 0;
    return NULL;
}

/* --- Static attribute accessor -------------------------------------- */

const char *sel_static_attr(const ir_node *el, const char *name)
{
    if (el == NULL || el->type != IR_ELEMENT) return NULL;
    for (size_t i = 0; i < el->element.n_attrs; i++) {
        const ir_attr *a = &el->element.attrs[i];
        if (a->name != NULL && strcmp(a->name, name) == 0) {
            return a->value == NULL ? "" : a->value;
        }
    }
    return NULL;
}

/* --- Walker state --------------------------------------------------- */

typedef struct {
    compile_arena    *arena;
    lua_State        *L;
    bucket_builder_t  by_id;
    bucket_builder_t  by_class;
    bucket_builder_t  by_tag;
    bucket_builder_t  by_attr_name;
    bucket_builder_t  all_bb;      /* single-bucket flat list for `all` */
    bucket_builder_t  includes_bb; /* single-bucket flat list for x-include */
    size_t            order;
} walker_t;

/* Split a class attribute value on whitespace and register each token in
 * the class bucket. The tokens themselves point into the original
 * attribute string, so we pass length + pointer separately to bb_push. */
static int split_classes(walker_t *w, const char *raw, const ir_node *el)
{
    const char *s = raw;
    while (*s != '\0') {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ||
               *s == '\f')
            s++;
        if (*s == '\0') break;
        const char *start = s;
        while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '\n' &&
               *s != '\r' && *s != '\f')
            s++;
        size_t len = (size_t)(s - start);
        if (len > 0) {
            if (bb_push(&w->by_class, start, len, el) != 0) return -1;
        }
    }
    return 0;
}

static int annotate_element(walker_t *w, ir_node *el,
                            const ir_node *parent, size_t depth,
                            const ir_node *chain_parent, size_t chain_branch,
                            bool is_chain_branch,
                            size_t match_branch, bool is_match_branch)
{
    w->order++;
    el->element.parent          = parent;
    el->element.chain_parent    = chain_parent;
    el->element.depth           = depth;
    el->element.order           = w->order;
    el->element.chain_branch    = chain_branch;
    el->element.match_branch    = match_branch;
    el->element.is_chain_branch = is_chain_branch;
    el->element.is_match_branch = is_match_branch;

    /* by_tag */
    if (el->element.tag_name != NULL) {
        if (bb_push(&w->by_tag, el->element.tag_name,
                    strlen(el->element.tag_name), el) != 0) return -1;
    }

    /* by_id / by_class / by_attr_name — all static attributes */
    for (size_t i = 0; i < el->element.n_attrs; i++) {
        const ir_attr *a = &el->element.attrs[i];
        if (a->name == NULL) continue;
        size_t nl = strlen(a->name);
        if (bb_push(&w->by_attr_name, a->name, nl, el) != 0) return -1;
        if (a->value == NULL || a->value[0] == '\0') continue;
        if (strcmp(a->name, "id") == 0) {
            if (bb_push(&w->by_id, a->value, strlen(a->value), el) != 0)
                return -1;
        } else if (strcmp(a->name, "class") == 0) {
            if (split_classes(w, a->value, el) != 0) return -1;
        }
    }

    /* Flat `all` bucket keyed by empty string for the fallback seed. */
    if (bb_push(&w->all_bb, "", 0, el) != 0) return -1;

    if (el->element.directives.include_expr != NULL) {
        if (bb_push(&w->includes_bb, "", 0, el) != 0) return -1;
    }
    return 0;
}

/* Forward-declare the recursive walkers. Chain wrappers are transparent
 * (their branches are annotated with the ENCLOSING element as parent).
 * x-match cases are transparent in the same sense, but their `parent` is
 * the x-match element itself so the source-visible structure survives. */
static int walk_children(walker_t *w, ir_node * const *children, size_t n,
                         const ir_node *parent, size_t depth);
static int walk_element(walker_t *w, ir_node *el, size_t depth);

static int walk_children(walker_t *w, ir_node * const *children, size_t n,
                         const ir_node *parent, size_t depth)
{
    for (size_t i = 0; i < n; i++) {
        ir_node *child = children[i];
        if (child == NULL) continue;
        if (child->type == IR_ELEMENT) {
            if (annotate_element(w, child, parent, depth,
                                 /*chain_parent=*/NULL,
                                 /*chain_branch=*/0,
                                 /*is_chain_branch=*/false,
                                 /*match_branch=*/0,
                                 /*is_match_branch=*/false) != 0) return -1;
            if (walk_element(w, child, depth) != 0) return -1;
            continue;
        }
        if (child->type == IR_CHAIN) {
            for (size_t bi = 0; bi < child->chain.n_branches; bi++) {
                ir_node *branch = child->chain.branches[bi].node;
                if (branch == NULL || branch->type != IR_ELEMENT) continue;
                if (annotate_element(w, branch, parent, depth,
                                     /*chain_parent=*/child,
                                     /*chain_branch=*/bi,
                                     /*is_chain_branch=*/true,
                                     0, false) != 0) return -1;
                if (walk_element(w, branch, depth) != 0) return -1;
            }
            continue;
        }
        /* text / comment / unknown — nothing to index */
    }
    return 0;
}

static int walk_element(walker_t *w, ir_node *el, size_t depth)
{
    /* x-match cases replace regular children. */
    if (el->element.directives.match != NULL) {
        const ir_match *m = el->element.directives.match;
        for (size_t bi = 0; bi < m->n_branches; bi++) {
            ir_node *branch = m->branches[bi].node;
            if (branch == NULL || branch->type != IR_ELEMENT) continue;
            if (annotate_element(w, branch, el, depth + 1,
                                 /*chain_parent=*/NULL,
                                 0, false,
                                 /*match_branch=*/bi,
                                 /*is_match_branch=*/true) != 0) return -1;
            if (walk_element(w, branch, depth + 1) != 0) return -1;
        }
        return 0;
    }
    return walk_children(w, el->element.children, el->element.n_children,
                         el, depth + 1);
}

sel_index *sel_build_index(compile_arena *arena, lua_State *L,
                           const ir_node *root)
{
    walker_t w = {0};
    w.arena = arena;
    w.L     = L;

    int rc = 0;
    if (root != NULL && root->type == IR_ROOT) {
        rc = walk_children(&w, root->root.children, root->root.n_children,
                           /*parent=*/NULL, /*depth=*/0);
    }
    if (rc != 0) {
        bb_free(&w.by_id);
        bb_free(&w.by_class);
        bb_free(&w.by_tag);
        bb_free(&w.by_attr_name);
        bb_free(&w.all_bb);
        bb_free(&w.includes_bb);
        return NULL;
    }

    sel_index *idx = (sel_index *)compile_arena_alloc(arena, L, sizeof(*idx));
    memset(idx, 0, sizeof(*idx));
    bb_commit(&w.by_id,       arena, L, &idx->by_id);
    bb_commit(&w.by_class,    arena, L, &idx->by_class);
    bb_commit(&w.by_tag,      arena, L, &idx->by_tag);
    bb_commit(&w.by_attr_name,arena, L, &idx->by_attr_name);

    /* Flatten the single-bucket `all` and `includes` builders. */
    size_t all_n = 0;
    if (w.all_bb.count > 0) all_n = w.all_bb.entries[0].count;
    idx->all = (const ir_node **)compile_arena_alloc(
        arena, L, sizeof(const ir_node *) * (all_n == 0 ? 1 : all_n));
    if (all_n > 0) {
        memcpy(idx->all, w.all_bb.entries[0].items,
               all_n * sizeof(const ir_node *));
    }
    idx->n_all = all_n;

    size_t inc_n = 0;
    if (w.includes_bb.count > 0) inc_n = w.includes_bb.entries[0].count;
    idx->includes = (const ir_node **)compile_arena_alloc(
        arena, L, sizeof(const ir_node *) * (inc_n == 0 ? 1 : inc_n));
    if (inc_n > 0) {
        memcpy(idx->includes, w.includes_bb.entries[0].items,
               inc_n * sizeof(const ir_node *));
    }
    idx->n_includes = inc_n;

    bb_free(&w.by_id);
    bb_free(&w.by_class);
    bb_free(&w.by_tag);
    bb_free(&w.by_attr_name);
    bb_free(&w.all_bb);
    bb_free(&w.includes_bb);
    return idx;
}
