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

#ifndef REFLOW_POOL_H
#define REFLOW_POOL_H

#include <lua.h>
#include <stddef.h>

/** Opaque handle for a memory pool. */
typedef struct pool_t pool_t;

/*
 * A pool allocates, reallocates and releases blocks of any size in any
 * order.  It owns an arena (arena.h) and cuts its blocks from the arena's
 * chunks: a block takes the power of two that holds it and a freed block is
 * reused for the next block of the same size class, all in plain C.  A chunk
 * whose blocks are all freed goes back to the arena, and blocks larger than
 * the size classes are large allocations of the arena.  Only taking a new
 * chunk or a large allocation makes a protected call.  Freeing a pointer the
 * pool did not allocate, or freeing it twice, is undefined.
 */

/*
 * Lifetime
 * ========
 */

/**
 * @brief Create a new memory pool.
 * On success the pool full userdata is left on the top of the Lua stack and
 * the corresponding pool_t pointer is returned.  The caller must keep the pool
 * userdata reachable from Lua (for example as an owner uservalue) for as long
 * as the pool is used, and must eventually release it with pool_delete.
 * The pool userdata carries no metatable of its own; the Lua bridge or the
 * owner installs the reflow.pool metatable whose __gc calls pool_delete.
 * @param L        Lua state that hosts the pool.
 * @param capacity Usable-size limit in bytes, or 0 for an unlimited pool.
 * @return The pool pointer on success, or NULL on failure with errno set to
 *         EINVAL (L is NULL) or ENOMEM (allocation failed).  On failure the
 *         Lua stack is restored to its original height.
 */
pool_t *pool_new(lua_State *L, size_t capacity);

/**
 * @brief Release the memory of a pool.
 * Drops the references of the pool's arena, so that every block of the pool
 * becomes collectable.  Idempotent: passing a NULL pool or an already-deleted
 * pool is a no-op.  Pointers into the pool must not be used afterwards.
 * @param L    Lua state that shares the global state used to create the pool.
 * @param pool Pool to delete, or NULL.
 */
void pool_delete(lua_State *L, pool_t *pool);

/*
 * Allocation and release
 * ======================
 */

/**
 * @brief Allocate a block with an explicit alignment.
 * @param pool      Target pool.
 * @param size      Number of usable bytes; 0 returns a distinct freeable block.
 * @param alignment 0 for the default alignment, otherwise a power of two.
 * @return The aligned usable pointer on success, or NULL on failure with errno
 *         set to EINVAL (invalid alignment) or ENOMEM (overflow, capacity
 *         exceeded, or allocation failure).
 */
void *pool_aligned_alloc(pool_t *pool, size_t size, size_t alignment);

/**
 * @brief Allocate a block with the default alignment.
 * Convenience wrapper around pool_aligned_alloc with alignment 0; it has no
 * standalone implementation symbol.
 */
#define pool_alloc(pool, size) pool_aligned_alloc((pool), (size), 0)

/**
 * @brief Allocate a zero-initialized block for count * size bytes.
 * Detects overflow of count * size before changing any pool state.
 * @param pool  Target pool.
 * @param count Element count.
 * @param size  Element size.
 * @return The aligned usable pointer on success, or NULL on failure with errno
 *         set to ENOMEM.
 */
void *pool_calloc(pool_t *pool, size_t count, size_t size);

/**
 * @brief Resize a block, preserving min(old_size, size) bytes of contents.
 * When mem is NULL the call behaves like pool_alloc.  When mem is not NULL and
 * size is 0 the block is freed and NULL is returned with errno preserved.  A
 * block that still holds size keeps its address.  On failure the old block and
 * pool state are left unchanged.
 * @param pool Target pool.
 * @param mem  Usable pointer previously returned by the pool, or NULL.
 * @param size New usable size in bytes.
 * @return The new aligned usable pointer on success, or NULL (see above) with
 *         errno set to ENOMEM on failure.
 */
void *pool_realloc(pool_t *pool, void *mem, size_t size);

/**
 * @brief Return a block to the pool for later reuse.
 * @param pool Target pool.
 * @param mem  Usable pointer previously returned by the pool, or NULL for a
 *             no-op.
 * @return 0.
 */
int pool_free(pool_t *pool, void *mem);

/*
 * Memory held
 * ===========
 */

/**
 * @brief Drop the chunks of the pool's arena that no block uses, while the
 * retained size exceeds size (see arena_compact).
 * @return The retained size after compaction.
 */
size_t pool_compact(pool_t *pool, size_t size);

/**
 * @brief Total size of the chunks and large allocations the pool holds.
 */
size_t pool_retained(const pool_t *pool);

#endif /* REFLOW_POOL_H */
