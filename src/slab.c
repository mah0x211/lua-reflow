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

#include "slab.h"

/* The maximum fundamental alignment, which objects are aligned to. */
union slab_max_align {
    long double a;
    intmax_t b;
    uintmax_t c;
    void *d;
    void (*e)(void);
};

struct slab_align_probe {
    char c;
    union slab_max_align u;
};

#define SLAB_ALIGNMENT offsetof(struct slab_align_probe, u)
#define SLAB_ROUND(n)  (((n) + SLAB_ALIGNMENT - 1) & ~(SLAB_ALIGNMENT - 1))

/* The front of a chunk links the chunks of the slab. */
#define SLAB_CHUNK_LINK SLAB_ROUND(sizeof(void *))

int slab_init(slab_t *slab, arena_t *arena, size_t object_size)
{
    size_t size =
        SLAB_ROUND(object_size < sizeof(void *) ? sizeof(void *) : object_size);

    if (object_size > arena_chunk_size(arena) ||
        size > arena_chunk_size(arena) - SLAB_CHUNK_LINK) {
        errno = EINVAL;
        return -1;
    }
    *slab = (slab_t){
        .arena       = arena,
        .object_size = size,
    };
    return 0;
}

void *slab_alloc(slab_t *slab)
{
    void *object = slab->free;

    if (object != NULL) {
        memcpy(&slab->free, object, sizeof(void *));
        return object;
    }
    if (slab->cur == NULL ||
        (size_t)(slab->end - slab->cur) < slab->object_size) {
        /* The rest of the current chunk is left unused. */
        char *chunk = (char *)arena_chunk_acquire(slab->arena);

        if (chunk == NULL) {
            return NULL;
        }
        memcpy(chunk, &slab->chunks, sizeof(void *));
        slab->chunks = chunk;
        slab->cur    = chunk + SLAB_CHUNK_LINK;
        slab->end    = chunk + arena_chunk_size(slab->arena);
    }
    object = slab->cur;
    slab->cur += slab->object_size;
    return object;
}

void slab_free(slab_t *slab, void *object)
{
    if (object != NULL) {
        memcpy(object, &slab->free, sizeof(void *));
        slab->free = object;
    }
}

void slab_reset(slab_t *slab)
{
    while (slab->chunks != NULL) {
        void *chunk = slab->chunks;

        memcpy(&slab->chunks, chunk, sizeof(void *));
        arena_chunk_release(slab->arena, chunk);
    }
    slab->free = NULL;
    slab->cur = slab->end = NULL;
}
