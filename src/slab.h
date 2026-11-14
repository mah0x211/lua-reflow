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

#ifndef REFLOW_SLAB_H
#define REFLOW_SLAB_H

#include <stddef.h>

#include "arena.h"

/*
 * A slab allocates objects of one size, such as the nodes of a compiled
 * template.  It cuts them without a header from chunks borrowed from an
 * arena, and reuses freed objects from a free list, all in plain C.  The
 * chunks stay with the slab until slab_reset gives them back to the arena.
 * Freeing an object the slab did not allocate, or freeing it twice, is
 * undefined.
 */
typedef struct slab_t {
    arena_t *arena;
    size_t object_size; /* rounded up to the default alignment */
    void *free;         /* freed objects */
    void *chunks;       /* borrowed chunks, linked through their first word */
    char *cur;          /* the next free byte of the current chunk */
    char *end;          /* the end of the current chunk */
} slab_t;

/**
 * @brief Initialize a slab of objects of object_size bytes on arena.
 * @return 0, or -1 with errno set to EINVAL when an object does not fit in
 *         a chunk of the arena.
 */
int slab_init(slab_t *slab, arena_t *arena, size_t object_size);

/**
 * @brief Allocate an object aligned to the default alignment.
 * @return The object, or NULL with errno set to ENOMEM.
 */
void *slab_alloc(slab_t *slab);

/**
 * @brief Return an object for reuse.  NULL is a no-op.
 */
void slab_free(slab_t *slab, void *object);

/**
 * @brief Give every chunk back to the arena.  Every object becomes invalid.
 */
void slab_reset(slab_t *slab);

#endif /* REFLOW_SLAB_H */
