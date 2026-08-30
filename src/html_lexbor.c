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

#include <lauxlib.h>
#include <lua.h>

#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <lexbor/html/tokenizer.h>
#if defined(__clang__)
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif

#include "compat.h"
#include "html_lexbor_internal.h"
#include "pool.h"
#include "reflow_util.h"

typedef struct reflow_html_lexbor_ctx_t {
    lua_State *L;
    pool_t *pool;
    int pool_ref;
    lxb_html_tokenizer_t *tokenizer;
    const char *html;
    size_t html_len;
    reflow_html_lexbor_token_f token_done;
    void *token_ctx;
    lxb_status_t status;
} reflow_html_lexbor_ctx_t;

static pool_t *active_pool;
static int allocator_ready;

static void *lexbor_pool_malloc(size_t size)
{
    if (active_pool == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return pool_alloc(active_pool, size);
}

static void *lexbor_pool_realloc(void *ptr, size_t size)
{
    if (active_pool == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return pool_realloc(active_pool, ptr, size);
}

static void *lexbor_pool_calloc(size_t count, size_t size)
{
    if (active_pool == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return pool_calloc(active_pool, count, size);
}

static void lexbor_pool_free(void *ptr)
{
    if (active_pool == NULL) {
        errno = EINVAL;
        return;
    }
    (void)pool_free(active_pool, ptr);
}

static lxb_status_t setup_allocator(void)
{
    lxb_status_t status = LXB_STATUS_OK;

    if (allocator_ready) {
        return LXB_STATUS_OK;
    }

    status = lexbor_memory_setup(lexbor_pool_malloc, lexbor_pool_realloc,
                                 lexbor_pool_calloc, lexbor_pool_free);
    if (status == LXB_STATUS_OK) {
        allocator_ready = 1;
    }
    return status;
}

static int begin_session(reflow_html_lexbor_ctx_t *ctx)
{
    ctx->pool = pool_new(ctx->L, 0);
    if (ctx->pool == NULL) {
        ctx->status = LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        return 0;
    }

    ctx->pool_ref = luaL_ref(ctx->L, LUA_REGISTRYINDEX);
    active_pool   = ctx->pool;
    return 1;
}

static int tokenize_operation(lua_State *L)
{
    reflow_html_lexbor_ctx_t *ctx =
        (reflow_html_lexbor_ctx_t *)lua_touserdata(L, lua_upvalueindex(1));

    if (!begin_session(ctx)) {
        return 0;
    }

    ctx->tokenizer = lxb_html_tokenizer_create();
    if (ctx->tokenizer == NULL) {
        ctx->status = LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        return 0;
    }

    ctx->status = lxb_html_tokenizer_init(ctx->tokenizer);
    if (ctx->status != LXB_STATUS_OK) {
        return 0;
    }

    lxb_html_tokenizer_callback_token_done_set(ctx->tokenizer, ctx->token_done,
                                               ctx->token_ctx);
    ctx->status = lxb_html_tokenizer_begin(ctx->tokenizer);
    if (ctx->status != LXB_STATUS_OK) {
        return 0;
    }

    ctx->status = lxb_html_tokenizer_chunk(
        ctx->tokenizer, (const lxb_char_t *)ctx->html, ctx->html_len);
    if (ctx->status != LXB_STATUS_OK) {
        return 0;
    }

    ctx->status = lxb_html_tokenizer_end(ctx->tokenizer);
    return 0;
}

static void cleanup_operation(lua_State *L, void *data)
{
    reflow_html_lexbor_ctx_t *ctx = (reflow_html_lexbor_ctx_t *)data;

    if (ctx->tokenizer != NULL) {
        ctx->tokenizer = lxb_html_tokenizer_destroy(ctx->tokenizer);
    }
    if (active_pool == ctx->pool) {
        active_pool = NULL;
    }
    pool_delete(L, ctx->pool);
    luaL_unref(L, LUA_REGISTRYINDEX, ctx->pool_ref);
    ctx->pool     = NULL;
    ctx->pool_ref = LUA_NOREF;
}

static int run_operation(lua_State *L, reflow_html_lexbor_ctx_t *ctx)
{
    ctx->status = setup_allocator();
    if (ctx->status != LXB_STATUS_OK) {
        return LUA_OK;
    }
    if (active_pool != NULL) {
        ctx->status = LXB_STATUS_ERROR_WRONG_STAGE;
        return LUA_OK;
    }

    return reflow_pcall_ex(L, tokenize_operation, ctx, 0, 0, cleanup_operation);
}

static reflow_html_lexbor_ctx_t
make_tokenize_context(lua_State *L, const char *html, size_t html_len,
                      reflow_html_lexbor_token_f token_done, void *token_ctx)
{
    reflow_html_lexbor_ctx_t ctx = {
        .L          = L,
        .pool_ref   = LUA_NOREF,
        .html       = html,
        .html_len   = html_len,
        .token_done = token_done,
        .token_ctx  = token_ctx,
        .status     = LXB_STATUS_OK,
    };

    return ctx;
}

static int run_tokenize_context(lua_State *L, reflow_html_lexbor_ctx_t *ctx,
                                lxb_status_t *lexbor_status)
{
    int status = run_operation(L, ctx);

    if (lexbor_status != NULL) {
        *lexbor_status = ctx->status;
    }
    return status;
}

int reflow_html_lexbor_tokenize(lua_State *L, const char *html, size_t html_len,
                                reflow_html_lexbor_token_f token_done,
                                void *token_ctx, lxb_status_t *lexbor_status)
{
    reflow_html_lexbor_ctx_t ctx = {0};

    if (L == NULL || token_done == NULL || lexbor_status == NULL ||
        (html == NULL && html_len != 0)) {
        errno = EINVAL;
        if (lexbor_status != NULL) {
            *lexbor_status = LXB_STATUS_ERROR_WRONG_ARGS;
        }
        return LUA_OK;
    }

    ctx = make_tokenize_context(L, html == NULL ? "" : html, html_len,
                                token_done, token_ctx);
    return run_tokenize_context(L, &ctx, lexbor_status);
}
