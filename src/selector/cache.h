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

#ifndef REFLOW_SELECTOR_CACHE_H
#define REFLOW_SELECTOR_CACHE_H

#include <lua.h>
#include <stddef.h>
#include "../error.h"
#include "parse.h"

/*
 * Bounded LRU cache for parsed selectors.  Sits between callers that
 * pass raw selector strings to render(...) and the parser proper.  Only
 * successfully parsed selectors are cached, so an attacker who spams
 * invalid input cannot bloat the working set; parse errors bubble out
 * unchanged from parseSelector.
 *
 * Storage layout:
 *   - The cache owns a persistent compile_arena (allocated as a Lua
 *     userdata anchored on a dedicated `lua_newthread` stack, same
 *     model as compile_arena_new).  Every cached compiled selector is
 *     bump-allocated from that arena.  Evicting entries only removes
 *     them from the lookup structure; the arena memory is reclaimed in
 *     bulk when the cache itself is destroyed.
 *   - A doubly-linked list threads entries in LRU order.
 *   - A flat array of pointers to entries is used for lookup; the
 *     expected working set is small (dozens of entries), so linear
 *     scan is fine — no hash needed.
 *
 * Lifecycle:
 *   sel_cache_new(L, max_size) — pushes a Lua userdata with __gc onto
 *     the stack and returns a pointer to the cache.  Caller decides
 *     how to anchor the userdata (typically a registry ref).
 *   sel_cache_resolve(cache, L, source, source_len, err) — returns a
 *     sel_compiled* for the string, hitting the cache when possible.
 *     A parse failure returns NULL and fills err.
 *   sel_cache_clear(cache) — evicts every entry (arena is preserved
 *     because we do not know when the surviving pointers held elsewhere
 *     will be dropped).
 */

typedef struct sel_cache sel_cache;

/*
 * Push a new sel_cache userdata onto L's stack.  max_size 0 disables
 * caching entirely (every call parses from scratch); values > 0 impose
 * a hard cap on the retained entry count.
 */
sel_cache *sel_cache_new(lua_State *L, size_t max_size);

/*
 * Return the compiled selector for `source`.  On cache miss the source
 * is parsed; only successfully parsed selectors are inserted so bad
 * input does not consume cache slots.  Returns NULL and fills err on
 * parse failure (err.type = "ReflowSelectorError").
 */
const sel_compiled *sel_cache_resolve(sel_cache *cache, lua_State *L,
                                      const char *source,
                                      size_t source_len,
                                      reflow_error *err);

/* Return the compiled selector currently cached for `source` without
 * updating recency.  NULL when absent. */
const sel_compiled *sel_cache_peek(sel_cache *cache,
                                   const char *source, size_t source_len);

/* Number of live entries. */
size_t sel_cache_size(const sel_cache *cache);

/* Maximum retained entries (as passed to sel_cache_new). */
size_t sel_cache_max_size(const sel_cache *cache);

/* Remove all entries.  The underlying arena is retained so outstanding
 * pointers into cached ASTs remain valid until the cache is collected. */
void sel_cache_clear(sel_cache *cache);

#endif /* REFLOW_SELECTOR_CACHE_H */
