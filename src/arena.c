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
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <lauxlib.h>

#include "arena.h"
#include "compat.h"

/** Default size of the userdata of a chunk. */
#define ARENA_DEFAULT_CHUNK_SIZE ((size_t)64 * 1024)

/** Page size used when the system does not report one. */
#define ARENA_FALLBACK_PAGE_SIZE ((size_t)4096)

/* The maximum fundamental alignment, which every block is aligned to. */
union arena_max_align {
    long double a;
    long long b;
    double c;
    void *d;
    void (*e)(void);
};

struct arena_align_probe {
    char c;
    union arena_max_align u;
};

#define ARENA_ALIGNMENT offsetof(struct arena_align_probe, u)

/* A chunk or a large allocation.  The header is at the front of its full
 * userdata; the usable region follows at the first default-aligned address
 * after the header and a back pointer to it. */
typedef struct arena_block_t {
    struct arena_block_t *prev;
    struct arena_block_t *next;
    int ref;      /* reference in the arena's reference table */
    size_t total; /* size of the userdata */
    char *data;   /* usable region */
    size_t size;  /* usable size */
} arena_block_t;

/* Room for the header, the back pointer, and the worst alignment padding. */
#define ARENA_BLOCK_OVERHEAD                                                   \
    (sizeof(arena_block_t) + sizeof(arena_block_t *) + ARENA_ALIGNMENT - 1)

struct arena_t {
    lua_State *L;
    int ref_L;  /* registry reference to the reference table, or LUA_NOREF */
    int fn_ref; /* the protected body in the reference table (Lua 5.1) */
    size_t chunk_total;     /* size of the userdata of a chunk */
    size_t chunk_size;      /* usable size of a chunk */
    size_t retained;        /* size of the referenced userdata */
    arena_block_t *free;    /* chunks kept for reuse */
    arena_block_t *cutting; /* chunks arena_alloc cuts from, current first */
    arena_block_t *large;   /* large allocations */
    char *cur;              /* the next free byte of the current chunk */
    char *end;              /* the end of the current chunk */
};

/* Arguments and results of the protected body. */
typedef struct {
    arena_t *arena;
    size_t chunk_total;
    size_t total;
    arena_block_t *block;
} arena_ctx_t;

static int arena_new_protected(lua_State *L);
static int arena_block_protected(lua_State *L);

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static size_t page_size(void)
{
    long size = sysconf(_SC_PAGESIZE);

    return size > 0 ? (size_t)size : ARENA_FALLBACK_PAGE_SIZE;
}

/* Round size up to a multiple of the page size, or return 0 on overflow. */
static size_t round_to_pages(size_t size)
{
    size_t page = page_size();

    if (size > SIZE_MAX - (page - 1)) {
        return 0;
    }
    return (size + page - 1) / page * page;
}

static arena_block_t *block_of(void *data)
{
    arena_block_t *block = NULL;

    memcpy(&block, (char *)data - sizeof(block), sizeof(block));
    return block;
}

/* Push the protected body without allocating: Lua 5.1 and LuaJIT read it from
 * the reference table, later versions push it as a light C function. */
static void push_protected(lua_State *L, arena_t *arena)
{
#if LUA_VERSION_NUM < 502
    lua_rawgeti(L, LUA_REGISTRYINDEX, arena->ref_L);
    lua_rawgeti(L, -1, arena->fn_ref);
    lua_remove(L, -2);
#else
    (void)arena;
    lua_pushcfunction(L, arena_block_protected);
#endif
}

/* -------------------------------------------------------------------------- */
/* Lifetime                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body of arena_new; may raise LUA_ERRMEM.
 *
 * Leaves the new arena userdata on the stack top.
 */
static int arena_new_protected(lua_State *L)
{
    arena_ctx_t *ctx = (arena_ctx_t *)lua_touserdata(L, 1);
    arena_t *arena   = NULL;

    lua_settop(L, 0);

    /* Initialize every field before any step that may collect garbage. */
    arena  = (arena_t *)lua_newuserdata(L, sizeof(arena_t));
    *arena = (arena_t){
        .L           = L,
        .ref_L       = LUA_NOREF,
        .fn_ref      = LUA_NOREF,
        .chunk_total = ctx->chunk_total,
        .chunk_size  = ctx->chunk_total - ARENA_BLOCK_OVERHEAD,
    };

    /* Keep the origin thread in the owner table, so that a thread -> arena
     * cycle can still be collected. */
    lua_createtable(L, 1, 0);
    lua_pushthread(L);
    lua_rawseti(L, -2, 1);
    reflow_setuservalue(L, -2);

    /* The reference table holds the chunks and the large allocations, and on
     * Lua 5.1 the protected body, created once here so that preparing a
     * protected call does not allocate.  It is registered last, so that a
     * failure before leaves nothing referenced from the registry. */
    lua_createtable(L, 0, 0);
#if LUA_VERSION_NUM < 502
    lua_pushcfunction(L, arena_block_protected);
    arena->fn_ref = luaL_ref(L, -2);
#endif
    arena->ref_L = luaL_ref(L, LUA_REGISTRYINDEX);
    ctx->arena   = arena;
    return 1;
}

arena_t *arena_new(lua_State *L, size_t chunk_size)
{
    arena_ctx_t ctx = {0};
    int base        = 0;

    if (L == NULL) {
        errno = EINVAL;
        return NULL;
    }
    ctx.chunk_total =
        round_to_pages(chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE);
    if (ctx.chunk_total == 0) {
        errno = ENOMEM;
        return NULL;
    }
    while (ctx.chunk_total <= ARENA_BLOCK_OVERHEAD) {
        ctx.chunk_total += page_size();
    }
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, arena_new_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 1, 0)) {
    case LUA_OK:
        return ctx.arena;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        /* A raw-table protected body has no other failure mode. */
        lua_error(L);
        return NULL; /* unreachable */
    }
}

void arena_delete(lua_State *L, arena_t *arena)
{
    if (L && arena && arena->ref_L != LUA_NOREF) {
        /* Clear the reference before unref so a re-entrant call is a no-op. */
        int ref      = arena->ref_L;
        arena->ref_L = LUA_NOREF;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        arena->free = arena->cutting = arena->large = NULL;
        arena->cur = arena->end = NULL;
        arena->retained         = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Blocks                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body that creates the userdata of a block and references
 * it from the reference table; may raise LUA_ERRMEM.
 */
static int arena_block_protected(lua_State *L)
{
    arena_ctx_t *ctx     = (arena_ctx_t *)lua_touserdata(L, 1);
    arena_t *arena       = ctx->arena;
    arena_block_t *block = NULL;
    uintptr_t data       = 0;
    int refs             = 0;

    lua_settop(L, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, arena->ref_L);
    refs   = lua_gettop(L);
    block  = (arena_block_t *)lua_newuserdata(L, ctx->total);
    data   = (uintptr_t)block + sizeof(*block) + sizeof(block);
    data   = (data + ARENA_ALIGNMENT - 1) & ~(uintptr_t)(ARENA_ALIGNMENT - 1);
    *block = (arena_block_t){
        .ref   = LUA_NOREF,
        .total = ctx->total,
        .data  = (char *)data,
        .size  = ctx->total - ARENA_BLOCK_OVERHEAD,
    };
    memcpy((char *)data - sizeof(block), &block, sizeof(block));
    block->ref = luaL_ref(L, refs);
    ctx->block = block;
    return 0;
}

/* Create a block whose userdata is total bytes. */
static arena_block_t *new_block(arena_t *arena, size_t total)
{
    lua_State *L    = arena->L;
    arena_ctx_t ctx = {
        .arena = arena,
        .total = total,
    };
    int base = lua_gettop(L);

    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }
    push_protected(L, arena);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 0, 0)) {
    case LUA_OK:
        lua_settop(L, base);
        arena->retained += total;
        return ctx.block;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        lua_error(L);
        return NULL; /* unreachable */
    }
}

/* Drop the reference to a block, so that the collector reclaims it. */
static void drop_block(arena_t *arena, arena_block_t *block)
{
    lua_State *L = arena->L;

    arena->retained -= block->total;
    lua_rawgeti(L, LUA_REGISTRYINDEX, arena->ref_L);
    luaL_unref(L, -1, block->ref);
    lua_pop(L, 1);
}

static void push_block(arena_block_t **list, arena_block_t *block)
{
    block->prev = NULL;
    block->next = *list;
    if (*list != NULL) {
        (*list)->prev = block;
    }
    *list = block;
}

static void unlink_block(arena_block_t **list, arena_block_t *block)
{
    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        *list = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
}

/* -------------------------------------------------------------------------- */
/* Chunks                                                                     */
/* -------------------------------------------------------------------------- */

size_t arena_chunk_size(const arena_t *arena)
{
    return arena->chunk_size;
}

/* Take a chunk kept for reuse, or create one. */
static arena_block_t *take_chunk(arena_t *arena)
{
    arena_block_t *chunk = arena->free;

    if (chunk != NULL) {
        unlink_block(&arena->free, chunk);
        return chunk;
    }
    return new_block(arena, arena->chunk_total);
}

void *arena_chunk_acquire(arena_t *arena)
{
    arena_block_t *chunk = take_chunk(arena);

    return chunk != NULL ? chunk->data : NULL;
}

void arena_chunk_release(arena_t *arena, void *chunk)
{
    if (chunk != NULL) {
        push_block(&arena->free, block_of(chunk));
    }
}

/* -------------------------------------------------------------------------- */
/* Allocation                                                                 */
/* -------------------------------------------------------------------------- */

void *arena_large_alloc(arena_t *arena, size_t size)
{
    size_t total         = 0;
    arena_block_t *block = NULL;

    if (size > SIZE_MAX - ARENA_BLOCK_OVERHEAD ||
        (total = round_to_pages(size + ARENA_BLOCK_OVERHEAD)) == 0) {
        errno = ENOMEM;
        return NULL;
    }
    block = new_block(arena, total);
    if (block == NULL) {
        return NULL;
    }
    push_block(&arena->large, block);
    return block->data;
}

void arena_large_free(arena_t *arena, void *mem)
{
    arena_block_t *block = NULL;

    if (mem == NULL) {
        return;
    }
    block = block_of(mem);
    unlink_block(&arena->large, block);
    drop_block(arena, block);
}

void *arena_alloc(arena_t *arena, size_t size, size_t alignment)
{
    uintptr_t cur = 0;

    if (alignment == 0) {
        alignment = ARENA_ALIGNMENT;
    } else if ((alignment & (alignment - 1)) != 0 ||
               alignment > ARENA_ALIGNMENT) {
        errno = EINVAL;
        return NULL;
    }
    if (size > arena->chunk_size / 4) {
        return arena_large_alloc(arena, size);
    }
    cur = ((uintptr_t)arena->cur + alignment - 1) & ~(uintptr_t)(alignment - 1);
    if (arena->cur == NULL || size > (size_t)((uintptr_t)arena->end - cur)) {
        /* The rest of the current chunk is left unused. */
        arena_block_t *chunk = take_chunk(arena);

        if (chunk == NULL) {
            return NULL;
        }
        push_block(&arena->cutting, chunk);
        arena->end = chunk->data + chunk->size;
        cur        = (uintptr_t)chunk->data;
    }
    arena->cur = (char *)cur + size;
    return (void *)cur;
}

void arena_reset(arena_t *arena)
{
    while (arena->cutting != NULL) {
        arena_block_t *chunk = arena->cutting;

        unlink_block(&arena->cutting, chunk);
        push_block(&arena->free, chunk);
    }
    while (arena->large != NULL) {
        arena_block_t *block = arena->large;

        unlink_block(&arena->large, block);
        drop_block(arena, block);
    }
    arena->cur = arena->end = NULL;
}

size_t arena_compact(arena_t *arena, size_t size)
{
    while (arena->retained > size && arena->free != NULL) {
        arena_block_t *chunk = arena->free;

        unlink_block(&arena->free, chunk);
        drop_block(arena, chunk);
    }
    return arena->retained;
}

size_t arena_retained(const arena_t *arena)
{
    return arena->retained;
}
