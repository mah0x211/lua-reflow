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
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <lauxlib.h>

#include "arena.h"
#include "compat.h"
#include "pool.h"
#include "pool_internal.h"

/* -------------------------------------------------------------------------- */
/* Layout                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * Union of the fundamental types whose strictest alignment the pool honors by
 * default.
 */
union pool_max_align {
    long double a;
    intmax_t b;
    uintmax_t c;
    void *d;
    void (*e)(void);
};

/**
 * Probe whose member offset yields the maximum fundamental alignment.
 */
struct pool_align_probe {
    char c;
    union pool_max_align u;
};

/** Default alignment used by pool_alloc and normal allocations. */
#define POOL_DEFAULT_ALIGNMENT offsetof(struct pool_align_probe, u)

#define POOL_ROUND(n)                                                          \
    (((n) + POOL_DEFAULT_ALIGNMENT - 1) & ~(POOL_DEFAULT_ALIGNMENT - 1))

/** Size of the userdata of a chunk: the smallest multiple of 16 KB that holds
 * a block of the largest size class.  Smaller chunks keep a compiled template
 * small and lower what each render allocates (measured for MEM-01). */
#define POOL_CHUNK_SIZE ((size_t)16 * 1024)

/** Size classes: powers of two from POOL_MIN_CLASS_SIZE to the largest. */
#define POOL_CLASSES        10
#define POOL_MIN_CLASS_SIZE 16
#define POOL_MAX_CLASS_SIZE (POOL_MIN_CLASS_SIZE << (POOL_CLASSES - 1))

/** Compile-time assertion helper. */
#define POOL_STATIC_ASSERT(cond, tag)                                          \
    typedef char pool_static_assert_##tag[(cond) ? 1 : -1]

POOL_STATIC_ASSERT(POOL_DEFAULT_ALIGNMENT != 0, default_alignment_nonzero);
POOL_STATIC_ASSERT((POOL_DEFAULT_ALIGNMENT & (POOL_DEFAULT_ALIGNMENT - 1)) == 0,
                   default_alignment_power_of_two);
POOL_STATIC_ASSERT(POOL_MIN_CLASS_SIZE % POOL_DEFAULT_ALIGNMENT == 0,
                   class_sizes_keep_alignment);

/* A chunk of the arena that the pool cuts blocks from.  The chunk record is
 * at the front of the region the arena lends. */
typedef struct pool_chunk_t {
    struct pool_chunk_t *prev;
    struct pool_chunk_t *next;
    size_t live; /* blocks in use */
    char *base;  /* the first block */
    char *cur;   /* the next free byte */
    char *end;   /* the end of the region */
} pool_chunk_t;

/* Kept before each block.  chunk is NULL for a large allocation.  word holds
 * the requested size, and for a block cut from a chunk, its size class and
 * whether it is free. */
typedef struct {
    pool_chunk_t *chunk;
    size_t word;
} pool_block_t;

#define POOL_HEADER_SIZE  POOL_ROUND(sizeof(pool_block_t))
#define POOL_WORD_BITS    (sizeof(size_t) * CHAR_BIT)
#define POOL_FREE_BIT     ((size_t)1 << (POOL_WORD_BITS - 1))
#define POOL_CLASS_SHIFT  (POOL_WORD_BITS - 8)
#define POOL_CLASS_MASK   ((size_t)0x7f)
#define POOL_SIZE_MASK    (((size_t)1 << POOL_CLASS_SHIFT) - 1)
#define POOL_CHUNK_RECORD POOL_ROUND(sizeof(pool_chunk_t))

/* A free block links the blocks of its size class through its own bytes. */
typedef struct pool_free_t {
    struct pool_free_t *prev;
    struct pool_free_t *next;
} pool_free_t;

POOL_STATIC_ASSERT(POOL_MIN_CLASS_SIZE >= sizeof(pool_free_t),
                   free_link_fits_the_smallest_class);
POOL_STATIC_ASSERT(POOL_CHUNK_SIZE >= 2 * POOL_MAX_CLASS_SIZE,
                   chunk_holds_the_largest_class);

struct pool_t {
    arena_t *arena;
    size_t capacity; /* usable-size limit, or 0 */
    size_t used;     /* requested sizes of the blocks in use */
    pool_chunk_t *chunks;
    pool_chunk_t *current; /* the chunk new blocks are cut from */
    pool_free_t *free[POOL_CLASSES];
    int deleted;
};

static pool_block_t *header_of(void *mem)
{
    return (pool_block_t *)(void *)((char *)mem - POOL_HEADER_SIZE);
}

static void *data_of(pool_block_t *block)
{
    return (char *)block + POOL_HEADER_SIZE;
}

static size_t block_class(const pool_block_t *block)
{
    return (block->word >> POOL_CLASS_SHIFT) & POOL_CLASS_MASK;
}

static size_t block_size(const pool_block_t *block)
{
    return block->chunk != NULL ? block->word & POOL_SIZE_MASK : block->word;
}

static size_t class_capacity(size_t k)
{
    return (size_t)POOL_MIN_CLASS_SIZE << k;
}

/* The size class of a size of at most POOL_MAX_CLASS_SIZE. */
static size_t class_of(size_t size)
{
    size_t k = 0;

    while (class_capacity(k) < size) {
        k++;
    }
    return k;
}

size_t pool_block_size(const void *mem)
{
    return block_size((const pool_block_t *)(const void *)((const char *)mem -
                                                           POOL_HEADER_SIZE));
}

/* -------------------------------------------------------------------------- */
/* Lifetime                                                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    size_t capacity;
    pool_t *pool;
    int status;
} pool_new_ctx_t;

/**
 * @brief Protected body of pool_new; may raise LUA_ERRMEM.
 *
 * Leaves the new pool userdata on the stack top, with its arena in its owner
 * table.
 */
static int pool_new_protected(lua_State *L)
{
    pool_new_ctx_t *ctx = (pool_new_ctx_t *)lua_touserdata(L, 1);
    pool_t *pool        = NULL;
    arena_t *arena      = NULL;

    lua_settop(L, 0);
    pool  = (pool_t *)lua_newuserdata(L, sizeof(pool_t));
    *pool = (pool_t){
        .capacity = ctx->capacity,
        .deleted  = 1,
    };
    lua_createtable(L, 1, 0);
    arena = arena_new(L, POOL_CHUNK_SIZE);
    if (arena == NULL) {
        ctx->status = errno;
        return 0;
    }
    /* The arena keeps the origin thread; the pool keeps the arena. */
    lua_rawseti(L, -2, 1);
    reflow_setuservalue(L, 1);
    pool->arena   = arena;
    pool->deleted = 0;
    ctx->pool     = pool;
    return 1;
}

pool_t *pool_new(lua_State *L, size_t capacity)
{
    pool_new_ctx_t ctx = {
        .capacity = capacity,
    };
    int base = 0;

    if (L == NULL) {
        errno = EINVAL;
        return NULL;
    }
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, pool_new_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 1, 0)) {
    case LUA_OK:
        if (ctx.pool == NULL) {
            lua_settop(L, base);
            errno = ctx.status;
            return NULL;
        }
        /* The pool userdata is left on the stack top for the caller. */
        return ctx.pool;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        /* A raw-table protected body has no other failure mode; surface it. */
        lua_error(L);
        return NULL; /* unreachable */
    }
}

void pool_delete(lua_State *L, pool_t *pool)
{
    if (L && pool && !pool->deleted) {
        size_t k = 0;

        pool->deleted = 1;
        arena_delete(L, pool->arena);
        pool->chunks  = NULL;
        pool->current = NULL;
        for (k = 0; k < POOL_CLASSES; k++) {
            pool->free[k] = NULL;
        }
        pool->used = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Chunks                                                                     */
/* -------------------------------------------------------------------------- */

static void unlink_free(pool_t *pool, size_t k, pool_free_t *node)
{
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        pool->free[k] = node->next;
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    }
}

/* Give a chunk whose blocks are all free back to the arena. */
static void release_chunk(pool_t *pool, pool_chunk_t *chunk)
{
    char *p = chunk->base;

    while (p < chunk->cur) {
        pool_block_t *block = (pool_block_t *)(void *)p;
        size_t k            = block_class(block);

        unlink_free(pool, k, (pool_free_t *)data_of(block));
        p += POOL_HEADER_SIZE + class_capacity(k);
    }
    if (chunk->prev != NULL) {
        chunk->prev->next = chunk->next;
    } else {
        pool->chunks = chunk->next;
    }
    if (chunk->next != NULL) {
        chunk->next->prev = chunk->prev;
    }
    arena_chunk_release(pool->arena, chunk);
}

/* Make a new chunk current.  The previous one is given back when no block
 * uses it. */
static pool_chunk_t *new_chunk(pool_t *pool)
{
    pool_chunk_t *previous = pool->current;
    char *region           = (char *)arena_chunk_acquire(pool->arena);
    pool_chunk_t *chunk    = (pool_chunk_t *)(void *)region;

    if (region == NULL) {
        return NULL;
    }
    *chunk = (pool_chunk_t){
        .next = pool->chunks,
        .base = region + POOL_CHUNK_RECORD,
        .cur  = region + POOL_CHUNK_RECORD,
        .end  = region + arena_chunk_size(pool->arena),
    };
    if (pool->chunks != NULL) {
        pool->chunks->prev = chunk;
    }
    pool->chunks  = chunk;
    pool->current = chunk;
    if (previous != NULL && previous->live == 0) {
        release_chunk(pool, previous);
    }
    return chunk;
}

/* -------------------------------------------------------------------------- */
/* Allocation                                                                 */
/* -------------------------------------------------------------------------- */

static void *alloc_small(pool_t *pool, size_t size)
{
    size_t k            = class_of(size);
    size_t need         = POOL_HEADER_SIZE + class_capacity(k);
    pool_free_t *node   = pool->free[k];
    pool_chunk_t *chunk = pool->current;
    pool_block_t *block = NULL;

    if (node != NULL) {
        unlink_free(pool, k, node);
        block       = header_of(node);
        block->word = size | (k << POOL_CLASS_SHIFT);
        block->chunk->live++;
        return node;
    }
    if (chunk == NULL || (size_t)(chunk->end - chunk->cur) < need) {
        /* The rest of the current chunk is left unused. */
        if ((chunk = new_chunk(pool)) == NULL) {
            return NULL;
        }
    }
    block  = (pool_block_t *)(void *)chunk->cur;
    *block = (pool_block_t){
        .chunk = chunk,
        .word  = size | (k << POOL_CLASS_SHIFT),
    };
    chunk->cur += need;
    chunk->live++;
    return data_of(block);
}

/* A large allocation of the arena, with the arena's block kept before the
 * header. */
static void *alloc_large(pool_t *pool, size_t size, size_t alignment)
{
    size_t front        = sizeof(void *) + POOL_HEADER_SIZE + alignment - 1;
    char *base          = NULL;
    uintptr_t data      = 0;
    pool_block_t *block = NULL;

    if (size > SIZE_MAX - front ||
        (base = (char *)arena_large_alloc(pool->arena, front + size)) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    data  = (uintptr_t)base + sizeof(void *) + POOL_HEADER_SIZE;
    data  = (data + alignment - 1) & ~(uintptr_t)(alignment - 1);
    block = header_of((void *)data);
    memcpy((char *)block - sizeof(void *), &base, sizeof(base));
    *block = (pool_block_t){
        .word = size,
    };
    return (void *)data;
}

/* Allocate without the capacity check, which the callers make. */
static void *alloc_block(pool_t *pool, size_t size, size_t alignment)
{
    if (alignment <= POOL_DEFAULT_ALIGNMENT && size <= POOL_MAX_CLASS_SIZE) {
        return alloc_small(pool, size);
    }
    return alloc_large(pool, size,
                       alignment > POOL_DEFAULT_ALIGNMENT ?
                           alignment :
                           POOL_DEFAULT_ALIGNMENT);
}

static void free_block(pool_t *pool, pool_block_t *block)
{
    pool_chunk_t *chunk = block->chunk;
    size_t k            = 0;
    pool_free_t *node   = NULL;

    if (chunk == NULL) {
        char *base = NULL;

        memcpy(&base, (char *)block - sizeof(void *), sizeof(base));
        arena_large_free(pool->arena, base);
        return;
    }
    k           = block_class(block);
    node        = (pool_free_t *)data_of(block);
    block->word = POOL_FREE_BIT | (k << POOL_CLASS_SHIFT);
    *node       = (pool_free_t){
        .next = pool->free[k],
    };
    if (pool->free[k] != NULL) {
        pool->free[k]->prev = node;
    }
    pool->free[k] = node;
    if (--chunk->live == 0 && chunk != pool->current) {
        release_chunk(pool, chunk);
    }
}

void *pool_aligned_alloc(pool_t *pool, size_t size, size_t alignment)
{
    void *mem = NULL;

    if (pool == NULL || pool->deleted) {
        errno = EINVAL;
        return NULL;
    } else if (alignment == 0) {
        alignment = POOL_DEFAULT_ALIGNMENT;
    } else if ((alignment & (alignment - 1)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (size > POOL_SIZE_MASK ||
        (pool->capacity != 0 && size > pool->capacity - pool->used)) {
        errno = ENOMEM;
        return NULL;
    }
    mem = alloc_block(pool, size, alignment);
    if (mem != NULL) {
        pool->used += size;
    }
    return mem;
}

void *pool_calloc(pool_t *pool, size_t count, size_t size)
{
    void *mem = NULL;

    if (pool == NULL) {
        errno = EINVAL;
        return NULL;
    } else if (size != 0 && count > SIZE_MAX / size) {
        /* count * size would overflow. */
        errno = ENOMEM;
        return NULL;
    }
    mem = pool_aligned_alloc(pool, count * size, 0);
    if (mem != NULL) {
        memset(mem, 0, count * size);
    }
    return mem;
}

void *pool_realloc(pool_t *pool, void *mem, size_t size)
{
    pool_block_t *block = NULL;
    size_t old          = 0;
    void *moved         = NULL;

    if (mem == NULL) {
        return pool_aligned_alloc(pool, size, 0);
    } else if (pool == NULL || pool->deleted) {
        errno = EINVAL;
        return NULL;
    } else if (size == 0) {
        int errnum = errno;

        (void)pool_free(pool, mem);
        errno = errnum;
        return NULL;
    }
    block = header_of(mem);
    old   = block_size(block);
    if (size > POOL_SIZE_MASK || (pool->capacity != 0 && size > old &&
                                  size - old > pool->capacity - pool->used)) {
        errno = ENOMEM;
        return NULL;
    }
    if (block->chunk != NULL ? size <= class_capacity(block_class(block)) :
                               size <= old) {
        /* The block still holds the new size. */
        block->word = block->chunk != NULL ?
                          size | (block_class(block) << POOL_CLASS_SHIFT) :
                          size;
        pool->used  = pool->used - old + size;
        return mem;
    }
    moved = alloc_block(pool, size, POOL_DEFAULT_ALIGNMENT);
    if (moved == NULL) {
        return NULL;
    }
    memcpy(moved, mem, old < size ? old : size);
    free_block(pool, block);
    pool->used = pool->used - old + size;
    return moved;
}

int pool_free(pool_t *pool, void *mem)
{
    if (pool != NULL && !pool->deleted && mem != NULL) {
        pool_block_t *block = header_of(mem);

        pool->used -= block_size(block);
        free_block(pool, block);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Memory held                                                                */
/* -------------------------------------------------------------------------- */

size_t pool_compact(pool_t *pool, size_t size)
{
    return pool->deleted ? 0 : arena_compact(pool->arena, size);
}

size_t pool_retained(const pool_t *pool)
{
    return pool->deleted ? 0 : arena_retained(pool->arena);
}
