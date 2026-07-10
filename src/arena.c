/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
// project
#include "arena.h"
// system
#include <string.h>

void arena_init(arena_t *a, char *base, size_t size)
{
    a->base  = base;
    a->size  = size;
    a->offset = 0;
}

void *arena_alloc(arena_t *a, size_t n)
{
    /* align to 8 bytes */
    size_t aligned = (a->offset + 7) & ~(size_t)7;
    if (aligned + n > a->size) {
        return NULL;
    }
    void *ptr = a->base + aligned;
    a->offset = aligned + n;
    memset(ptr, 0, n);
    return ptr;
}

void arena_reset(arena_t *a)
{
    a->offset = 0;
}
