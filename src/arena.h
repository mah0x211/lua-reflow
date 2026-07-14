/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_ARENA_H
#define REFLOW_ARENA_H

#include <stddef.h>

/*
 * Render-scope bump allocator (04-runtime.md §5).
 *
 * The arena manages a singly-linked list of chunks. When the current chunk
 * is exhausted, a new chunk is malloc-allocated and prepended; every chunk
 * is released together on arena_destroy. Callers can also pre-seed the
 * arena with a caller-owned buffer via arena_init.
 */

typedef struct arena_chunk {
    struct arena_chunk *next;
    size_t              size;    /* usable bytes in this chunk */
    size_t              offset;  /* bytes already handed out */
    /* raw storage follows for heap-allocated chunks; for the initial
     * caller-owned buffer, storage lives outside this header. */
} arena_chunk;

typedef struct arena {
    /* Initial (caller-owned) chunk. May be NULL. */
    char        *base;
    size_t       size;
    size_t       offset;
    /* Overflow chain of heap-allocated chunks (most recent first). */
    arena_chunk *heap_chunks;
    /* Preferred size for newly-allocated overflow chunks. */
    size_t       grow_size;
} arena_t;

/* Initialize arena with a pre-allocated buffer (caller owns base).
 * Overflow chunks are allocated via malloc and freed by arena_destroy. */
void arena_init(arena_t *a, char *base, size_t size);

/* Bump-allocate `n` bytes. Grows via malloc if the current chunk is full;
 * returns NULL only on malloc failure. */
void *arena_alloc(arena_t *a, size_t n);

/* Reset offset to 0 in every chunk (keeps chunks for reuse). */
void arena_reset(arena_t *a);

/* Release all heap-allocated overflow chunks. The initial caller-owned
 * buffer is NOT freed. */
void arena_destroy(arena_t *a);

#endif
