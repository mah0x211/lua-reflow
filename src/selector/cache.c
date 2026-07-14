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
#include "cache.h"
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include "../compile_arena.h"

#define SEL_CACHE_MT "reflow.sel_cache"

typedef struct cache_entry {
    const char           *key;
    size_t                key_len;
    const sel_compiled   *sel;
    struct cache_entry   *prev;
    struct cache_entry   *next;
} cache_entry;

struct sel_cache {
    /* Backing arena for cached ASTs. Anchored on `arena_ref` in the
     * registry so its lifetime is bounded by this userdata's GC. */
    int            arena_ref;
    compile_arena *arena;

    /* Doubly linked LRU list (head = most-recently-used). */
    cache_entry *head;
    cache_entry *tail;

    /* Linear array of live entries.  Simple pointer array on the
     * heap; hot lookup does a linear scan.  Working sets are small
     * (max_size default = 128), so a hash accelerator is unwarranted. */
    cache_entry **items;
    size_t        count;
    size_t        cap;
    size_t        max_size;
};

/* --- Userdata metatable ---------------------------------------------- */

static int cache_gc(lua_State *L)
{
    sel_cache *c = (sel_cache *)luaL_checkudata(L, 1, SEL_CACHE_MT);
    if (c->arena_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, c->arena_ref);
        c->arena_ref = LUA_NOREF;
    }
    for (size_t i = 0; i < c->count; i++) {
        free(c->items[i]);
    }
    free(c->items);
    c->items = NULL;
    c->count = 0;
    c->cap   = 0;
    c->head  = NULL;
    c->tail  = NULL;
    return 0;
}

static void ensure_mt(lua_State *L)
{
    if (luaL_newmetatable(L, SEL_CACHE_MT)) {
        lua_pushcfunction(L, cache_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_pop(L, 1);
}

/* --- Public entry points -------------------------------------------- */

sel_cache *sel_cache_new(lua_State *L, size_t max_size)
{
    ensure_mt(L);
    sel_cache *c = (sel_cache *)lua_newuserdata(L, sizeof(*c));
    memset(c, 0, sizeof(*c));
    c->max_size  = max_size;
    c->arena_ref = LUA_NOREF;

    /* Allocate the arena on the top of stack, then move it into the
     * registry via luaL_ref so its lifetime is bounded by the cache. */
    c->arena = compile_arena_new(L, 4096);
    c->arena_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, SEL_CACHE_MT);
    lua_setmetatable(L, -2);
    return c;
}

/* Search the entries array for `key` in linear time.  Returns index or
 * (size_t)-1 when absent.  Kept in a helper so the promotion logic in
 * resolve doesn't repeat the loop. */
static size_t find_index(sel_cache *c, const char *key, size_t key_len)
{
    for (size_t i = 0; i < c->count; i++) {
        if (c->items[i]->key_len == key_len &&
            memcmp(c->items[i]->key, key, key_len) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static void unlink_entry(sel_cache *c, cache_entry *e)
{
    if (e->prev) e->prev->next = e->next;
    else         c->head       = e->next;
    if (e->next) e->next->prev = e->prev;
    else         c->tail       = e->prev;
    e->prev = e->next = NULL;
}

static void push_front(sel_cache *c, cache_entry *e)
{
    e->prev = NULL;
    e->next = c->head;
    if (c->head) c->head->prev = e;
    c->head = e;
    if (c->tail == NULL) c->tail = e;
}

static void promote(sel_cache *c, cache_entry *e)
{
    if (c->head == e) return;
    unlink_entry(c, e);
    push_front(c, e);
}

static void evict_tail(sel_cache *c)
{
    cache_entry *e = c->tail;
    if (e == NULL) return;
    unlink_entry(c, e);
    /* Remove from items[] by swapping with the last live slot. */
    for (size_t i = 0; i < c->count; i++) {
        if (c->items[i] == e) {
            c->items[i] = c->items[c->count - 1];
            c->count--;
            break;
        }
    }
    free(e);
}

static int push_item(sel_cache *c, cache_entry *e)
{
    if (c->count == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 8;
        cache_entry **nb =
            (cache_entry **)realloc(c->items, ncap * sizeof(*nb));
        if (!nb) return -1;
        c->items = nb;
        c->cap   = ncap;
    }
    c->items[c->count++] = e;
    return 0;
}

const sel_compiled *sel_cache_peek(sel_cache *cache,
                                   const char *source, size_t source_len)
{
    size_t idx = find_index(cache, source, source_len);
    if (idx == (size_t)-1) return NULL;
    return cache->items[idx]->sel;
}

const sel_compiled *sel_cache_resolve(sel_cache *cache, lua_State *L,
                                      const char *source,
                                      size_t source_len,
                                      reflow_error *err)
{
    /* When caching is disabled the caller still gets a normal parse so
     * they see the identical error surface either way. */
    if (cache->max_size == 0) {
        return selector_parse(cache->arena, L, source, source_len, err);
    }

    size_t idx = find_index(cache, source, source_len);
    if (idx != (size_t)-1) {
        promote(cache, cache->items[idx]);
        return cache->items[idx]->sel;
    }

    /* Parse into the cache's arena FIRST so we only insert on success —
     * bad input never bloats the LRU or the arena above its normal
     * working set. */
    sel_compiled *sel = selector_parse(cache->arena, L,
                                       source, source_len, err);
    if (sel == NULL) return NULL;

    /* Copy the key into the arena so the entry does not alias caller
     * memory; entries live longer than the immediate call. */
    char *kdup = (char *)compile_arena_alloc(cache->arena, L,
                                             source_len + 1);
    memcpy(kdup, source, source_len);
    kdup[source_len] = '\0';

    cache_entry *e = (cache_entry *)calloc(1, sizeof(*e));
    if (!e) {
        err->type    = "ReflowSelectorError";
        err->message = "selector cache: out of memory";
        err->reason  = "syntax";
        return NULL;
    }
    e->key     = kdup;
    e->key_len = source_len;
    e->sel     = sel;
    push_front(cache, e);
    if (push_item(cache, e) != 0) {
        unlink_entry(cache, e);
        free(e);
        err->type    = "ReflowSelectorError";
        err->message = "selector cache: out of memory";
        err->reason  = "syntax";
        return NULL;
    }
    if (cache->count > cache->max_size) {
        evict_tail(cache);
    }
    return sel;
}

size_t sel_cache_size(const sel_cache *cache) { return cache->count; }
size_t sel_cache_max_size(const sel_cache *cache) { return cache->max_size; }

void sel_cache_clear(sel_cache *cache)
{
    for (size_t i = 0; i < cache->count; i++) {
        free(cache->items[i]);
    }
    cache->count = 0;
    cache->head  = NULL;
    cache->tail  = NULL;
    /* The arena is intentionally NOT reset: outstanding pointers to
     * cached ASTs may still be reachable via other paths (e.g. a
     * resolved candidate list held by a render call in progress).
     * Memory is reclaimed when the cache userdata is GC'd. */
}
