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

#ifndef REFLOW_SELECTOR_INDEX_H
#define REFLOW_SELECTOR_INDEX_H

#include <lua.h>
#include <stdbool.h>
#include <stddef.h>
#include "../compile_arena.h"
#include "../ir.h"

/*
 * Compile-time bucketed index over a compiled template's IR. Each bucket
 * is a chunk of "seed" candidates for a specific CSS anchor (id, class,
 * tag, attribute-name) so the resolver can pick a small set without
 * scanning the whole tree.
 *
 * Buckets are stored as parallel arrays: the flat `entries` array holds
 * every bucket back-to-back and `keys` records the (key_str, key_len,
 * offset, count) tuple that identifies each bucket. Lookup is a linear
 * scan across `keys` — template sizes are typically small enough that
 * an open-hash structure adds more overhead than it saves.
 *
 * Side effect: sel_build_index annotates every element with parent /
 * depth / order / chain_branch / match_branch / is_chain_branch /
 * is_match_branch. The `parent` link follows the source structure —
 * chain-wrapped branches point at the ELEMENT that encloses the chain
 * (not the chain node itself), matching the JS reference's contract.
 */

typedef struct {
    const char *key;
    size_t      key_len;
    size_t      offset;      /* index into entries[] */
    size_t      count;
} sel_bucket_key;

typedef struct {
    sel_bucket_key  *keys;
    size_t           n_keys;
    const ir_node **entries;
    size_t          n_entries;
} sel_bucket;

typedef struct {
    sel_bucket by_id;
    sel_bucket by_class;
    sel_bucket by_tag;
    sel_bucket by_attr_name;
    /* Elements that carry an x-include directive, in document order. */
    const ir_node **includes;
    size_t          n_includes;
    /* Every element in the tree, in document order. Used as a fall-back
     * seed when a selector's rightmost compound cannot anchor on any of
     * the four indexed axes. */
    const ir_node **all;
    size_t          n_all;
} sel_index;

/*
 * Build the selector index for `root` and annotate every element with
 * structural back-pointers. All storage is bump-allocated from arena.
 * Returns the newly built index; the returned pointer is valid for the
 * lifetime of the arena.
 */
sel_index *sel_build_index(compile_arena *arena, lua_State *L,
                           const ir_node *root);

/*
 * Fetch the candidate list for `key` from a bucket. When the key is
 * absent the returned pointer is NULL and *n is set to 0.
 */
const ir_node **sel_bucket_lookup(const sel_bucket *b,
                                  const char *key, size_t key_len,
                                  size_t *n);

/* Return the value of the static attribute `name` (case-sensitive) on
 * `el`, or NULL if absent. Zero-length values are returned as an empty
 * string so callers can distinguish presence from absence. */
const char *sel_static_attr(const ir_node *el, const char *name);

#endif /* REFLOW_SELECTOR_INDEX_H */
