/* MIT license — Copyright (C) 2026 Masatoshi Fukunaga */
#ifndef REFLOW_ARENA_H
#define REFLOW_ARENA_H

#include <lua.h>
#include <stddef.h>

#include "compile_arena.h"

/*
 * Operation-scope bump allocator backed by GC-visible Lua userdata chunks.
 * arena_init() leaves its owner userdata rooted on L's stack until
 * arena_destroy(); arena_bind() borrows an already-rooted compile arena.
 */
typedef struct arena {
    lua_State     *L;
    compile_arena *owner;
    int            owner_stack_index; /* positive absolute index, or 0 if bound */
    char          *base;              /* optional caller-owned initial buffer */
    size_t         size;
    size_t         offset;
} arena_t;

/* Creates and roots an internal compile_arena on L's stack. */
void arena_init(arena_t *a, lua_State *L, char *base, size_t size);

/* Borrows owner; the caller remains responsible for keeping it rooted. */
void arena_bind(arena_t *a, lua_State *L, compile_arena *owner,
                char *base, size_t size);

/* Zero-initialized bump allocation. OOM may throw through Lua. */
void *arena_alloc(arena_t *a, size_t n);

/* Reset only the optional initial buffer; userdata chunks remain append-only. */
void arena_reset(arena_t *a);

/* Removes an arena_init-created owner userdata from L's stack. */
void arena_destroy(arena_t *a);

#endif
