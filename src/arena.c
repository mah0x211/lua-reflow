/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
// project
#include "arena.h"
// system
#include <stdlib.h>
#include <string.h>

#define ARENA_DEFAULT_GROW  (64 * 1024)

void arena_init(arena_t *a, char *base, size_t size)
{
    a->base        = base;
    a->size        = size;
    a->offset      = 0;
    a->heap_chunks = NULL;
    a->grow_size   = ARENA_DEFAULT_GROW;
}

/* Round `n` up to the next 8-byte boundary. */
static size_t align_up(size_t n)
{
    return (n + 7u) & ~(size_t)7;
}

static void *chunk_bump(arena_chunk *c, size_t n)
{
    size_t aligned = align_up(c->offset);
    if (aligned + n > c->size) {
        return NULL;
    }
    /* Chunk storage begins right after the header. */
    char *storage = (char *)(c + 1);
    void *ptr = storage + aligned;
    c->offset = aligned + n;
    memset(ptr, 0, n);
    return ptr;
}

void *arena_alloc(arena_t *a, size_t n)
{
    /* Try the initial caller-owned buffer first. */
    if (a->base != NULL) {
        size_t aligned = align_up(a->offset);
        if (aligned + n <= a->size) {
            void *ptr = a->base + aligned;
            a->offset = aligned + n;
            memset(ptr, 0, n);
            return ptr;
        }
    }
    /* Try the current head heap chunk. */
    if (a->heap_chunks != NULL) {
        void *ptr = chunk_bump(a->heap_chunks, n);
        if (ptr != NULL) {
            return ptr;
        }
    }
    /* Grow: allocate a new chunk large enough for `n`. */
    size_t chunk_size = a->grow_size;
    if (chunk_size < n) {
        chunk_size = n;
    }
    arena_chunk *c = (arena_chunk *)malloc(sizeof(arena_chunk) + chunk_size);
    if (c == NULL) {
        return NULL;
    }
    c->next   = a->heap_chunks;
    c->size   = chunk_size;
    c->offset = 0;
    a->heap_chunks = c;
    return chunk_bump(c, n);
}

void arena_reset(arena_t *a)
{
    a->offset = 0;
    for (arena_chunk *c = a->heap_chunks; c != NULL; c = c->next) {
        c->offset = 0;
    }
}

void arena_destroy(arena_t *a)
{
    arena_chunk *c = a->heap_chunks;
    while (c != NULL) {
        arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    a->heap_chunks = NULL;
    a->offset      = 0;
}
