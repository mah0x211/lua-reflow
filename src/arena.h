/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_ARENA_H
#define REFLOW_ARENA_H

#include <stddef.h>

/*
 * Render-scope bump allocator (04-runtime.md §5).
 * All allocations within a single render() call come from this arena;
 * arena_reset frees everything at once (no per-allocation free, no
 * fragmentation).
 */
typedef struct arena {
    char  *base;
    size_t offset;
    size_t size;
} arena_t;

/* Initialize arena with a pre-allocated buffer (caller owns base). */
void arena_init(arena_t *a, char *base, size_t size);

/* Bump-allocate `n` bytes; returns NULL if out of space. */
void *arena_alloc(arena_t *a, size_t n);

/* Reset offset to 0 (reuse for next render). */
void arena_reset(arena_t *a);

#endif
