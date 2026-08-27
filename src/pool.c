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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "compat.h"
#include "pool.h"
#include "pool_internal.h"

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * Private representation of a memory pool.
 *
 * The internal state table (used/unused tables plus bookkeeping) lives in the
 * Lua registry under ref_L; the pool userdata itself carries no metatable, so
 * the owner releases the pool explicitly through pool_delete.
 */
struct pool_t {
    /* Lua state (origin thread) that hosts the pool. */
    lua_State *L;
    /* Registry reference to the internal state table, or LUA_NOREF. */
    int ref_L;
    /* Usable-size limit in bytes, or 0 for an unlimited pool. */
    size_t capacity;
    /* Total usable size currently allocated. */
    size_t used;
};

/**
 * Private header stored at the front of every block full userdata, followed by
 * alignment padding and the usable region.
 */
/**
 * Shared context passed to every protected body through a light userdata.
 *
 * Each operation reads only the inputs it needs and writes only its outputs;
 * unused fields keep their zero-initialized value.
 */
typedef struct pool_ctx_t {
    /* Target pool for most operations; created pool for pool_new. */
    pool_t *pool;
    /* pool_new: capacity limit. */
    size_t capacity;
    /* free/realloc/detach: target usable pointer. */
    void *data;
    /* alloc: usable size; realloc: new usable size. */
    size_t size;
    /* alloc: requested alignment. */
    size_t alignment;
    /* Output: 0 on success, otherwise an errno-style code. */
    int status;
    /* Output: aligned usable pointer for alloc and realloc. */
    void *result;
    /* Output: detached block for pool_detach. */
    pool_mem_t *mem;
} pool_ctx_t;

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * Integer keys of the slots stored in the internal state table.
 */
enum {
    STATE_USED_MEM_BY_BLOCK = 1, /* data pointer -> block (strong) */
    STATE_UNUSED_MEM_BY_SIZE,    /* size-class key -> chain head (weak value) */
    STATE_UNUSED_MEM_NEXT,       /* block -> next block in chain (weak kv) */
    STATE_CACHE_FN,              /* pre-created best-effort caching function */
    STATE_SLOT_COUNT = STATE_CACHE_FN
};

/**
 * Union of the largest fundamental types, used to derive the default
 * allocation alignment without relying on C11 max_align_t.
 */
union pool_max_align {
    long double a;
    intmax_t b;
    uintmax_t c;
    void *d;
    void (*e)(void);
};

/**
 * Probe whose member offset yields the maximum fundamental alignment.
 */
struct pool_align_probe {
    char c;
    union pool_max_align u;
};

/** Default alignment used by pool_alloc and normal allocations. */
#define POOL_DEFAULT_ALIGNMENT offsetof(struct pool_align_probe, u)

/** Compile-time assertion helper. */
#define POOL_STATIC_ASSERT(cond, tag)                                          \
    typedef char pool_static_assert_##tag[(cond) ? 1 : -1]

POOL_STATIC_ASSERT(POOL_DEFAULT_ALIGNMENT != 0, default_alignment_nonzero);
POOL_STATIC_ASSERT((POOL_DEFAULT_ALIGNMENT & (POOL_DEFAULT_ALIGNMENT - 1)) == 0,
                   default_alignment_pow2);

/* Keep the slot keys shared with the bridge in sync with the private enum. */
POOL_STATIC_ASSERT(STATE_USED_MEM_BY_BLOCK == POOL_STATE_USED_MEM_BY_BLOCK,
                   state_slot_used);
POOL_STATIC_ASSERT(STATE_UNUSED_MEM_BY_SIZE == POOL_STATE_UNUSED_MEM_BY_SIZE,
                   state_slot_unused_by_size);
POOL_STATIC_ASSERT(STATE_UNUSED_MEM_NEXT == POOL_STATE_UNUSED_MEM_NEXT,
                   state_slot_unused_next);

/** Length of the fixed-size (size, alignment) key used for the unused chain. */
#define POOL_SIZE_KEY_LEN (sizeof(size_t) * 2)

/* Forward declaration: the caching body is stored in the state table and also
 * invoked from release_used_block. */
static int cache_unused_block_protected(lua_State *L);

/* -------------------------------------------------------------------------- */
/* Small helpers                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Compute the total userdata size needed for a block.
 *
 * Accounts for the header, worst-case alignment padding, and the usable region
 * while rejecting size or arithmetic that cannot be represented.
 *
 * @param size      Usable size in bytes.
 * @param alignment Normalized alignment (a power of two).
 * @return The total byte size, or 0 on overflow or when size exceeds
 *         PTRDIFF_MAX.
 */
static size_t compute_block_size(size_t size, size_t alignment)
{
    size_t header = sizeof(struct pool_mem_t);
    size_t front  = 0;

    /* Reject sizes that cannot be represented as a signed difference. */
    if (size > (size_t)PTRDIFF_MAX) {
        return 0;
    }
    /* header + alignment padding, watching for overflow. */
    if (alignment - 1 > SIZE_MAX - header) {
        return 0;
    }
    front = header + (alignment - 1);
    /* front + usable size, watching for overflow. */
    if (size > SIZE_MAX - front) {
        return 0;
    }
    return front + size;
}

/**
 * @brief Encode (size, alignment) into a fixed-length lookup key.
 *
 * @param buf       Destination buffer of POOL_SIZE_KEY_LEN bytes.
 * @param size      Usable size.
 * @param alignment Normalized alignment.
 */
static void make_size_key(char *buf, size_t size, size_t alignment)
{
    memcpy(buf, &size, sizeof(size));
    memcpy(buf + sizeof(size), &alignment, sizeof(alignment));
}

void pool_push_state(lua_State *L, pool_t *pool)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, pool->ref_L);
}

/* -------------------------------------------------------------------------- */
/* Unused-block caching                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Best-effort protected body that caches a freed block for reuse.
 *
 * Inserts the block at the head of its size-class chain.  Stack arguments are
 * 1 = internal state table and 2 = block userdata.  May raise LUA_ERRMEM,
 * which the caller deliberately ignores because caching is optional.
 */
static int cache_unused_block_protected(lua_State *L)
{
    pool_mem_t *mem             = (pool_mem_t *)lua_touserdata(L, 2);
    char key[POOL_SIZE_KEY_LEN] = {0};

    make_size_key(key, mem->size, mem->alignment);

    /* old_head = unused_mem_by_size[key] */
    lua_rawgeti(L, 1, STATE_UNUSED_MEM_BY_SIZE);
    lua_pushlstring(L, key, POOL_SIZE_KEY_LEN);
    lua_rawget(L, -2);

    /* Link this block ahead of any existing head: next_tbl[mem] = old_head. */
    if (!lua_isnil(L, -1)) {
        lua_rawgeti(L, 1, STATE_UNUSED_MEM_NEXT);
        lua_pushvalue(L, 2);
        lua_pushvalue(L, -3);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    /* unused_mem_by_size[key] = mem */
    lua_pushlstring(L, key, POOL_SIZE_KEY_LEN);
    lua_pushvalue(L, 2);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Internal state table setup                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Push a fresh table whose __mode metafield is set to mode.
 */
static void push_weak_table(lua_State *L, const char *mode)
{
    lua_createtable(L, 0, 0);
    lua_createtable(L, 0, 1);
    lua_pushstring(L, mode);
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
}

/**
 * @brief Build the internal state table and leave it on the stack top.
 *
 * May raise LUA_ERRMEM, which the caller captures inside a protected call.
 */
static void build_state_table(lua_State *L)
{
    lua_createtable(L, STATE_SLOT_COUNT, 0);

    /* used_mem_by_block: strong values keyed by the usable data pointer. */
    lua_createtable(L, 0, 0);
    lua_rawseti(L, -2, STATE_USED_MEM_BY_BLOCK);

    /* unused_mem_by_size: weak values. */
    push_weak_table(L, "v");
    lua_rawseti(L, -2, STATE_UNUSED_MEM_BY_SIZE);

    /* unused_mem_next: weak keys and values. */
    push_weak_table(L, "kv");
    lua_rawseti(L, -2, STATE_UNUSED_MEM_NEXT);

    /* cache function: pre-created so releasing a block can schedule caching
     * without allocating (which matters on Lua 5.1). */
    lua_pushcfunction(L, cache_unused_block_protected);
    lua_rawseti(L, -2, STATE_CACHE_FN);
}

/* -------------------------------------------------------------------------- */
/* Block acquisition and release                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Remove and return an unused block matching (size, alignment).
 *
 * On success the block userdata is left on the stack top and 1 is returned; on
 * miss the stack is left unchanged and 0 is returned.  May raise LUA_ERRMEM
 * while building the size-class key.
 *
 * @param state_idx Absolute stack index of the internal state table.
 */
static int take_unused_block(lua_State *L, int state_idx, pool_t *pool,
                             size_t size, size_t alignment)
{
    char key[POOL_SIZE_KEY_LEN] = {0};
    int by_size                 = 0;
    int key_idx                 = 0;
    int head                    = 0;
    int next_tbl                = 0;
    pool_mem_t *mem             = NULL;

    make_size_key(key, size, alignment);

    /* head = unused_mem_by_size[key] */
    lua_rawgeti(L, state_idx, STATE_UNUSED_MEM_BY_SIZE);
    by_size = lua_gettop(L);
    lua_pushlstring(L, key, POOL_SIZE_KEY_LEN);
    key_idx = lua_gettop(L);
    lua_pushvalue(L, key_idx);
    lua_rawget(L, by_size);
    if (lua_isnil(L, -1)) {
        /* No cached block for this size class. */
        lua_pop(L, 3);
        return 0;
    }
    head = lua_gettop(L);
    mem  = (pool_mem_t *)lua_touserdata(L, head);

    /* next = unused_mem_next[head] */
    lua_rawgeti(L, state_idx, STATE_UNUSED_MEM_NEXT);
    next_tbl = lua_gettop(L);
    lua_pushvalue(L, head);
    lua_rawget(L, next_tbl);

    /* Establish the strong used-side reference first.  If this insertion
     * raises for lack of memory, the unused chain is still unchanged. */
    mem->pool = pool;
    mem->ref  = LUA_NOREF;
    lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
    lua_pushlightuserdata(L, mem->data);
    lua_pushvalue(L, head);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    /* Promote next to the size-class head: unused_mem_by_size[key] = next. */
    lua_pushvalue(L, key_idx);
    lua_pushvalue(L, -2);
    lua_rawset(L, by_size);

    /* Detach head from the chain: unused_mem_next[head] = nil. */
    lua_pushvalue(L, head);
    lua_pushnil(L);
    lua_rawset(L, next_tbl);
    lua_pop(L, 1);

    /* Leave only the reused block on the stack top. */
    lua_remove(L, next_tbl);
    lua_remove(L, key_idx);
    lua_remove(L, by_size);
    return 1;
}

/**
 * @brief Create a fresh block userdata and leave it on the stack top.
 *
 * The block carries no metatable; the Lua bridge attaches reflow.pool.mem when
 * a block is detached.  May raise LUA_ERRMEM.  The caller must have validated
 * the block size via compute_block_size.
 */
static void create_new_block(lua_State *L, pool_t *pool, size_t size,
                             size_t alignment)
{
    size_t block_size = compute_block_size(size, alignment);
    pool_mem_t *mem   = (pool_mem_t *)lua_newuserdata(L, block_size);
    uintptr_t base    = 0;
    uintptr_t aligned = 0;

    *mem = (pool_mem_t){
        .pool      = pool,
        .size      = size,
        .alignment = alignment,
        .ref       = LUA_NOREF,
    };

    /* Place the usable region at the first aligned offset after the header. */
    base      = (uintptr_t)mem + sizeof(pool_mem_t);
    aligned   = (base + alignment - 1) & ~(uintptr_t)(alignment - 1);
    mem->data = (void *)aligned;
}

/**
 * @brief Obtain a used block of (size, alignment).
 *
 * Reuses a matching unused block when available, otherwise creates a new one,
 * registers it as used, and adds its usable size to the accounting.  Leaves
 * the block userdata on the stack top and returns its handle.  A failure
 * raises LUA_ERRMEM before any accounting change.  The caller must have
 * validated the block size and capacity.
 *
 * @param state_idx Absolute stack index of the internal state table.
 */
static pool_mem_t *acquire_used_block(lua_State *L, int state_idx, pool_t *pool,
                                      size_t size, size_t alignment)
{
    pool_mem_t *mem = NULL;

    if (take_unused_block(L, state_idx, pool, size, alignment)) {
        mem = (pool_mem_t *)lua_touserdata(L, -1);
    } else {
        create_new_block(L, pool, size, alignment);
        mem = (pool_mem_t *)lua_touserdata(L, -1);
        lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
        lua_pushlightuserdata(L, mem->data);
        lua_pushvalue(L, -3);
        lua_rawset(L, -3);
        lua_pop(L, 1);
    }
    pool->used += size;
    return mem;
}

/**
 * @brief Release a used block back to the pool.
 *
 * Drops the strong used-side reference and updates accounting first (neither
 * allocates), then schedules best-effort caching for reuse.  Because the
 * essential work never allocates, this function never raises.
 *
 * @param state_idx Absolute stack index of the internal state table.
 * @param mem_idx   Absolute stack index of the block userdata to release.
 */
static void release_used_block(lua_State *L, int state_idx, int mem_idx,
                               pool_t *pool)
{
    pool_mem_t *mem = (pool_mem_t *)lua_touserdata(L, mem_idx);
    void *data      = mem->data;
    size_t size     = mem->size;

    /* Guaranteed phase (no allocation): remove the used-side reference and
     * update accounting, so the block is released even if caching fails. */
    lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
    lua_pushlightuserdata(L, data);
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    pool->used -= size;

    /* Best-effort phase: cache the block for reuse using the pre-created
     * function, discarding any out-of-memory failure. */
    lua_rawgeti(L, state_idx, STATE_CACHE_FN);
    lua_pushvalue(L, state_idx);
    lua_pushvalue(L, mem_idx);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        lua_pop(L, 1);
    }
}

/* -------------------------------------------------------------------------- */
/* Lifetime                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body of pool_new; may raise LUA_ERRMEM.
 *
 * Leaves the new pool userdata on the stack top and stores the pool pointer in
 * the shared context.
 */
static int pool_new_protected(lua_State *L)
{
    pool_ctx_t *ctx = (pool_ctx_t *)lua_touserdata(L, 1);
    pool_t *pool    = NULL;

    lua_settop(L, 0);

    /* Create the pool userdata and initialize every field before any step
     * that might trigger the garbage collector. */
    pool  = (pool_t *)lua_newuserdata(L, sizeof(pool_t));
    *pool = (pool_t){
        .L        = L,
        .ref_L    = LUA_NOREF,
        .capacity = ctx->capacity,
        .used     = 0,
    };

    /* Keep the origin thread in the pool's owner table.  Unlike the registry
     * state reference, this permits a thread -> pool cycle to be collected. */
    lua_createtable(L, 1, 0);
    lua_pushthread(L);
    lua_rawseti(L, -2, 1);
    reflow_setuservalue(L, -2);

    /* Build the internal state table and register it last, so a failure before
     * luaL_ref leaves nothing referenced from the registry. */
    build_state_table(L);
    pool->ref_L = luaL_ref(L, LUA_REGISTRYINDEX);
    ctx->pool   = pool;
    return 1;
}

pool_t *pool_new(lua_State *L, size_t capacity)
{
    pool_ctx_t ctx = {
        .capacity = capacity,
    };
    int base = 0;

    if (L == NULL) {
        errno = EINVAL;
        return NULL;
    }
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, pool_new_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 1, 0)) {
    case LUA_OK:
        /* The pool userdata is left on the stack top for the caller. */
        return ctx.pool;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        /* A raw-table protected body has no other failure mode; surface it. */
        lua_error(L);
        return NULL; /* unreachable */
    }
}

void pool_delete(lua_State *L, pool_t *pool)
{
    if (L && pool && pool->ref_L != LUA_NOREF) {
        /* Clear the reference before unref so a re-entrant call is a no-op. */
        int ref     = pool->ref_L;
        pool->ref_L = LUA_NOREF;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
}

/* -------------------------------------------------------------------------- */
/* Allocation                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body of pool_aligned_alloc; may raise LUA_ERRMEM.
 */
static int pool_alloc_protected(lua_State *L)
{
    pool_ctx_t *ctx  = (pool_ctx_t *)lua_touserdata(L, 1);
    pool_t *pool     = ctx->pool;
    size_t size      = ctx->size;
    size_t alignment = ctx->alignment;
    pool_mem_t *mem  = NULL;
    int state_idx    = 0;

    lua_settop(L, 0);

    /* Validate representable size and capacity before touching pool state. */
    if (compute_block_size(size, alignment) == 0) {
        ctx->status = ENOMEM;
        return 0;
    } else if (pool->capacity != 0 && size > pool->capacity - pool->used) {
        ctx->status = ENOMEM;
        return 0;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, pool->ref_L);
    state_idx   = lua_gettop(L);
    mem         = acquire_used_block(L, state_idx, pool, size, alignment);
    ctx->result = mem->data;
    return 0;
}

void *pool_aligned_alloc(pool_t *pool, size_t size, size_t alignment)
{
    pool_ctx_t ctx = {
        .pool = pool,
        .size = size,
    };
    lua_State *L = NULL;
    int base     = 0;

    if (pool == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (alignment == 0) {
        ctx.alignment = POOL_DEFAULT_ALIGNMENT;
    } else if ((alignment & (alignment - 1)) == 0) {
        ctx.alignment = alignment;
    } else {
        errno = EINVAL;
        return NULL;
    }

    L    = pool->L;
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, pool_alloc_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 0, 0)) {
    case LUA_OK:
        lua_settop(L, base);
        if (ctx.status != 0) {
            errno = ctx.status;
            return NULL;
        }
        return ctx.result;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        lua_error(L);
        return NULL; /* unreachable */
    }
}

void *pool_calloc(pool_t *pool, size_t count, size_t size)
{
    size_t total = 0;
    void *mem    = NULL;

    if (pool == NULL) {
        errno = EINVAL;
        return NULL;
    } else if (size != 0 && count > SIZE_MAX / size) {
        /* count * size would overflow. */
        errno = ENOMEM;
        return NULL;
    }

    total = count * size;
    mem   = pool_aligned_alloc(pool, total, 0);
    if (mem == NULL) {
        return NULL;
    }
    if (total > 0) {
        memset(mem, 0, total);
    }
    return mem;
}

/* -------------------------------------------------------------------------- */
/* Release                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body of pool_free.
 *
 * Performs only non-allocating operations, so it never raises LUA_ERRMEM.
 */
static int pool_free_protected(lua_State *L)
{
    pool_ctx_t *ctx = (pool_ctx_t *)lua_touserdata(L, 1);
    pool_t *pool    = ctx->pool;
    int state_idx   = 0;
    int mem_idx     = 0;

    lua_settop(L, 0);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pool->ref_L);
    state_idx = lua_gettop(L);

    /* mem = used_mem_by_block[data] */
    lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
    lua_pushlightuserdata(L, ctx->data);
    lua_rawget(L, -2);
    lua_replace(L, -2);
    if (lua_isnil(L, -1)) {
        /* Not owned by this pool: double free or foreign pointer. */
        ctx->status = EINVAL;
        return 0;
    }
    mem_idx = lua_gettop(L);
    release_used_block(L, state_idx, mem_idx, pool);
    return 0;
}

int pool_free(pool_t *pool, void *mem)
{
    pool_ctx_t ctx = {
        .pool = pool,
        .data = mem,
    };
    lua_State *L = NULL;
    int base     = 0;
    int errnum   = errno;

    if (pool == NULL) {
        errno = EINVAL;
        return -1;
    } else if (mem == NULL) {
        return 0;
    }

    L    = pool->L;
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return -1;
    }

    lua_pushcfunction(L, pool_free_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 0, 0)) {
    case LUA_OK:
        lua_settop(L, base);
        if (ctx.status != 0) {
            errno = ctx.status;
            return -1;
        }
        errno = errnum;
        return 0;

    case LUA_ERRMEM:
        /* Releasing does not allocate, so this path is defensive only. */
        lua_settop(L, base);
        errno = ENOMEM;
        return -1;

    default:
        lua_error(L);
        return -1; /* unreachable */
    }
}

/* -------------------------------------------------------------------------- */
/* Realloc                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body of pool_realloc; may raise LUA_ERRMEM.
 *
 * Assumes a non-NULL block and a non-zero target size.
 */
static int pool_realloc_protected(lua_State *L)
{
    pool_ctx_t *ctx     = (pool_ctx_t *)lua_touserdata(L, 1);
    pool_t *pool        = ctx->pool;
    size_t new_size     = ctx->size;
    pool_mem_t *old_mem = NULL;
    pool_mem_t *new_mem = NULL;
    size_t old_size     = 0;
    size_t copy         = 0;
    size_t new_used     = 0;
    int state_idx       = 0;
    int old_idx         = 0;

    lua_settop(L, 0);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pool->ref_L);
    state_idx = lua_gettop(L);

    /* old_mem = used_mem_by_block[data] */
    lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
    lua_pushlightuserdata(L, ctx->data);
    lua_rawget(L, -2);
    lua_replace(L, -2);
    if (lua_isnil(L, -1)) {
        ctx->status = EINVAL;
        return 0;
    }
    old_idx  = lua_gettop(L);
    old_mem  = (pool_mem_t *)lua_touserdata(L, old_idx);
    old_size = old_mem->size;

    /* Same usable size: nothing to move. */
    if (old_size == new_size) {
        ctx->result = old_mem->data;
        return 0;
    }

    /* Capacity check on the resulting usage total. */
    if (pool->capacity != 0) {
        new_used = pool->used - old_size;
        if (new_size > pool->capacity - new_used) {
            ctx->status = ENOMEM;
            return 0;
        }
    }

    /* Overflow check for the new block. */
    if (compute_block_size(new_size, POOL_DEFAULT_ALIGNMENT) == 0) {
        ctx->status = ENOMEM;
        return 0;
    }

    /* Acquire the new block while old_mem stays referenced on the stack; a
     * failure here raises before any state change, leaving the old block
     * intact. */
    new_mem = acquire_used_block(L, state_idx, pool, new_size,
                                 POOL_DEFAULT_ALIGNMENT);

    /* Preserve the overlapping contents. */
    copy = old_size < new_size ? old_size : new_size;
    if (copy > 0) {
        memcpy(new_mem->data, old_mem->data, copy);
    }

    /* Record success before releasing the old block: its best-effort caching
     * must not turn a completed resize into a reported failure. */
    ctx->result = new_mem->data;
    release_used_block(L, state_idx, old_idx, pool);
    return 0;
}

void *pool_realloc(pool_t *pool, void *mem, size_t size)
{
    pool_ctx_t ctx = {
        .pool = pool,
        .data = mem,
        .size = size,
    };
    lua_State *L = NULL;
    int base     = 0;

    if (pool == NULL) {
        errno = EINVAL;
        return NULL;
    } else if (mem == NULL) {
        return pool_alloc(pool, size);
    } else if (size == 0) {
        pool_free(pool, mem);
        return NULL;
    }

    L    = pool->L;
    base = lua_gettop(L);
    if (!lua_checkstack(L, 12)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, pool_realloc_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 0, 0)) {
    case LUA_OK:
        lua_settop(L, base);
        if (ctx.status != 0) {
            errno = ctx.status;
            return NULL;
        }
        return ctx.result;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        lua_error(L);
        return NULL; /* unreachable */
    }
}

/* -------------------------------------------------------------------------- */
/* Detach and detached-block release                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Protected body of pool_detach; may raise LUA_ERRMEM.
 */
static int pool_detach_protected(lua_State *L)
{
    pool_ctx_t *ctx = (pool_ctx_t *)lua_touserdata(L, 1);
    pool_t *pool    = ctx->pool;
    pool_mem_t *mem = NULL;
    size_t size     = 0;
    int state_idx   = 0;
    int mem_idx     = 0;

    lua_settop(L, 0);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pool->ref_L);
    state_idx = lua_gettop(L);

    /* mem = used_mem_by_block[data] */
    lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
    lua_pushlightuserdata(L, ctx->data);
    lua_rawget(L, -2);
    lua_replace(L, -2);
    if (lua_isnil(L, -1)) {
        ctx->status = EINVAL;
        return 0;
    }
    mem_idx = lua_gettop(L);
    mem     = (pool_mem_t *)lua_touserdata(L, mem_idx);
    size    = mem->size;

    /* Move ownership to a registry reference so the block outlives the pool. */
    lua_pushvalue(L, mem_idx);
    mem->ref  = luaL_ref(L, LUA_REGISTRYINDEX);
    mem->pool = NULL;

    /* Remove from used_mem_by_block and update accounting. */
    lua_rawgeti(L, state_idx, STATE_USED_MEM_BY_BLOCK);
    lua_pushlightuserdata(L, ctx->data);
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    pool->used -= size;

    ctx->mem = mem;
    return 0;
}

pool_mem_t *pool_detach(pool_t *pool, void *mem)
{
    pool_ctx_t ctx = {
        .pool = pool,
        .data = mem,
    };
    lua_State *L = NULL;
    int base     = 0;

    if (pool == NULL || mem == NULL) {
        errno = EINVAL;
        return NULL;
    }

    L    = pool->L;
    base = lua_gettop(L);
    if (!lua_checkstack(L, 8)) {
        errno = ENOMEM;
        return NULL;
    }

    lua_pushcfunction(L, pool_detach_protected);
    lua_pushlightuserdata(L, &ctx);
    switch (lua_pcall(L, 1, 0, 0)) {
    case LUA_OK:
        lua_settop(L, base);
        if (ctx.status != 0) {
            errno = ctx.status;
            return NULL;
        }
        return ctx.mem;

    case LUA_ERRMEM:
        lua_settop(L, base);
        errno = ENOMEM;
        return NULL;

    default:
        lua_error(L);
        return NULL; /* unreachable */
    }
}

void *pool_mem_data(pool_mem_t *mem)
{
    return mem != NULL ? mem->data : NULL;
}

size_t pool_mem_size(const pool_mem_t *mem)
{
    return mem != NULL ? mem->size : 0;
}

void pool_mem_push(lua_State *L, pool_mem_t *mem)
{
    if (mem == NULL || mem->ref == LUA_NOREF) {
        lua_pushnil(L);
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, mem->ref);
}

void pool_mem_free(lua_State *L, pool_mem_t *mem)
{
    int ref = mem->ref;

    /* Clear the reference before unref so a re-entrant call is a no-op. */
    mem->ref = LUA_NOREF;
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
}
