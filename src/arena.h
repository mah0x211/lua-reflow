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

#ifndef REFLOW_ARENA_H
#define REFLOW_ARENA_H

#include <lua.h>
#include <stddef.h>

/** Opaque handle for an arena. */
typedef struct arena_t arena_t;

/*
 * An arena owns the memory blocks (chunks) that the allocators built on it
 * cut their blocks from.  Each chunk and each large allocation is a full
 * userdata referenced from the arena, so the collector reclaims them with
 * the arena, or when the arena drops them.  Only creating a chunk or a large
 * allocation makes a protected call; every other operation is plain C.  The
 * operations report failures with NULL or -1 and errno, and raise no Lua
 * error.
 */

/**
 * @brief Create an arena.
 * On success the arena full userdata is left on the top of the Lua stack, and
 * the caller must keep it reachable (for example as an owner uservalue) for
 * as long as the arena is used, and release it with arena_delete.  The arena
 * userdata carries no metatable of its own.
 * @param L          Lua state that hosts the arena.
 * @param chunk_size Size of the userdata of a chunk, rounded up to a multiple
 *                   of the page size, or 0 for the default.
 * @return The arena, or NULL with errno set to EINVAL (L is NULL) or ENOMEM;
 *         the Lua stack is restored on failure.
 */
arena_t *arena_new(lua_State *L, size_t chunk_size);

/**
 * @brief Drop the references of an arena to its chunks and large
 * allocations, so that they become collectable.  Idempotent.  Pointers into
 * the arena must not be used afterwards.
 */
void arena_delete(lua_State *L, arena_t *arena);

/**
 * @brief Cut size bytes from the current chunk.  A request larger than a
 * quarter of a chunk becomes a large allocation.  Blocks are not released
 * one by one; arena_reset releases them all.
 * @param alignment 0 for the default alignment, otherwise a power of two no
 *                  larger than the default.
 * @return The block, or NULL with errno set to EINVAL (bad alignment) or
 *         ENOMEM.
 */
void *arena_alloc(arena_t *arena, size_t size, size_t alignment);

/**
 * @brief Allocate a block of its own, aligned to the default alignment, that
 * arena_large_free releases.
 * @return The block, or NULL with errno set to ENOMEM.
 */
void *arena_large_alloc(arena_t *arena, size_t size);

/**
 * @brief Release a block of arena_large_alloc.  NULL is a no-op.
 */
void arena_large_free(arena_t *arena, void *mem);

/**
 * @brief Usable size of a chunk handed out by arena_chunk_acquire.
 */
size_t arena_chunk_size(const arena_t *arena);

/**
 * @brief Hand out a whole chunk, reusing a released one when there is one.
 * @return The usable region of arena_chunk_size bytes, aligned to the default
 *         alignment, or NULL with errno set to ENOMEM.
 */
void *arena_chunk_acquire(arena_t *arena);

/**
 * @brief Take back a chunk of arena_chunk_acquire.  It is kept for reuse
 * until arena_compact drops it.  NULL is a no-op.
 */
void arena_chunk_release(arena_t *arena, void *chunk);

/**
 * @brief Release every block cut by arena_alloc and every large allocation.
 * The chunks are kept for reuse.
 */
void arena_reset(arena_t *arena);

/**
 * @brief While the retained size exceeds size, drop the chunks kept for
 * reuse.  A chunk in use is never dropped, so the result may stay above
 * size, and dropping a whole chunk may go below it.  The memory returns to
 * the system when the collector reclaims the chunks.
 * @return The retained size after compaction.
 */
size_t arena_compact(arena_t *arena, size_t size);

/**
 * @brief Total size of the userdata of the chunks and large allocations the
 * arena references.
 */
size_t arena_retained(const arena_t *arena);

#endif /* REFLOW_ARENA_H */
