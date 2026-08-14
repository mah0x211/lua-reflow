/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#include "arena.h"
#include "checked.h"

#include <string.h>

#define ARENA_DEFAULT_GROW (64 * 1024)

void arena_bind(arena_t *a, lua_State *L, compile_arena *owner,
                char *base, size_t size)
{
    a->L                 = L;
    a->owner             = owner;
    a->owner_stack_index = 0;
    a->base              = base;
    a->size              = size;
    a->offset            = 0;
}

void arena_init(arena_t *a, lua_State *L, char *base, size_t size)
{
    compile_arena *owner = compile_arena_new(L, ARENA_DEFAULT_GROW);
    arena_bind(a, L, owner, base, size);
    /* The owner was just pushed, so this positive index stays stable while
     * callers push and pop values above it. */
    a->owner_stack_index = lua_gettop(L);
}

void *arena_alloc(arena_t *a, size_t n)
{
    size_t aligned;
    size_t end;

    if (!reflow_size_align(a->offset, 8, &aligned) ||
        !reflow_size_add(aligned, n, &end)) {
        return NULL;
    }
    if (a->base != NULL && end <= a->size) {
        void *ptr = a->base + aligned;
        a->offset = end;
        memset(ptr, 0, n);
        return ptr;
    }

    void *ptr = compile_arena_alloc(a->owner, a->L, n);
    if (ptr != NULL) memset(ptr, 0, n);
    return ptr;
}

void arena_reset(arena_t *a)
{
    a->offset = 0;
}

void arena_destroy(arena_t *a)
{
    if (a->owner_stack_index > 0) {
        lua_remove(a->L, a->owner_stack_index);
    }
    a->L                 = NULL;
    a->owner             = NULL;
    a->owner_stack_index = 0;
    a->base              = NULL;
    a->size              = 0;
    a->offset            = 0;
}
