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

/** Opaque handle for a single memory block owned or detached by a pool. */
typedef struct pool_mem_t pool_mem_t;

/*
 * Lifetime
 * ========
 */

/**
 * @brief Create a new memory pool.
 *
 * On success the pool full userdata is left on the top of the Lua stack and
 * the corresponding pool_t pointer is returned.  The caller must keep the pool
 * userdata reachable from Lua (for example as an owner uservalue) for as long
 * as the pool is used, and must eventually release it with pool_delete.
 *
 * The pool userdata carries no metatable of its own; the Lua bridge or the
 * owner installs the reflow.pool metatable whose __gc calls pool_delete.
 *
 * @param L        Lua state that hosts the pool.
 * @param capacity Usable-size limit in bytes, or 0 for an unlimited pool.
 * @return The pool pointer on success, or NULL on failure with errno set to
 *         EINVAL (L is NULL) or ENOMEM (allocation failed).  On failure the
 *         Lua stack is restored to its original height.
 */
pool_t *pool_new(lua_State *L, size_t capacity);

/**
 * @brief Release the internal state of a pool.
 *
 * Unrefs the pool's internal state table so that the pool and every block it
 * still owns become collectable.  The call is idempotent: passing a NULL pool
 * or an already-deleted pool is a no-op.  Detached blocks are unaffected and
 * must be released separately with pool_mem_free.
 *
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
 *
 * @param pool      Target pool.
 * @param size      Number of usable bytes; 0 returns a distinct freeable block.
 * @param alignment 0 for the default alignment, otherwise a power of two.
 * @return The aligned usable pointer on success, or NULL on failure with errno
 *         set to EINVAL (invalid pool or alignment) or ENOMEM (overflow,
 *         capacity exceeded, or allocation failure).
 */
void *pool_aligned_alloc(pool_t *pool, size_t size, size_t alignment);

/**
 * @brief Allocate a block with the default alignment.
 *
 * Convenience wrapper around pool_aligned_alloc with alignment 0; it has no
 * standalone implementation symbol.
 */
#define pool_alloc(pool, size) pool_aligned_alloc((pool), (size), 0)

/**
 * @brief Allocate a zero-initialized block for count * size bytes.
 *
 * Detects overflow of count * size before changing any pool state.
 *
 * @param pool  Target pool.
 * @param count Element count.
 * @param size  Element size.
 * @return The aligned usable pointer on success, or NULL on failure with errno
 *         set to EINVAL or ENOMEM.
 */
void *pool_calloc(pool_t *pool, size_t count, size_t size);

/**
 * @brief Resize a block, preserving min(old_size, size) bytes of contents.
 *
 * When mem is NULL the call behaves like pool_alloc.  When mem is not NULL and
 * size is 0 the block is freed and NULL is returned with errno preserved.  On
 * failure the old block and pool state are left unchanged.
 *
 * @param pool Target pool.
 * @param mem  Usable pointer previously returned by the pool, or NULL.
 * @param size New usable size in bytes.
 * @return The new aligned usable pointer on success, or NULL (see above) with
 *         errno set to EINVAL or ENOMEM on failure.
 */
void *pool_realloc(pool_t *pool, void *mem, size_t size);

/**
 * @brief Return a block to the pool for later reuse.
 *
 * @param pool Target pool.
 * @param mem  Usable pointer to release, or NULL for a successful no-op.
 * @return 0 on success, or -1 with errno set to EINVAL when mem is not owned
 *         by the pool.
 */
int pool_free(pool_t *pool, void *mem);

/*
 * Detach
 * ======
 */

/**
 * @brief Detach a block, transferring ownership to the caller.
 *
 * The detached block survives independently of the pool until pool_mem_free is
 * called on it.
 *
 * @param pool Target pool.
 * @param mem  Usable pointer to detach.
 * @return The block handle on success, or NULL with errno set to EINVAL when
 *         mem is not owned by the pool.
 */
pool_mem_t *pool_detach(pool_t *pool, void *mem);

/**
 * @brief Return the aligned usable pointer of a detached block.
 */
void *pool_mem_data(pool_mem_t *mem);

/**
 * @brief Return the usable size of a detached block.
 */
size_t pool_mem_size(const pool_mem_t *mem);

/**
 * @brief Free a detached block.
 *
 * @param L   Lua state sharing the global state that produced the block.
 * @param mem Block previously returned by pool_detach.
 *
 * L and mem must be valid, mem must belong to the same Lua global state, and
 * the block must still be C-owned.  The mem pointer is invalid after return.
 */
void pool_mem_free(lua_State *L, pool_mem_t *mem);

#endif /* REFLOW_POOL_H */
