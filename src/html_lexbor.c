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

#include <lauxlib.h>
#include <lua.h>

#if defined(__clang__)
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
#include <lexbor/html/tag.h>
#include <lexbor/html/tokenizer.h>
#if defined(__clang__)
# pragma clang diagnostic pop
#elif defined(__GNUC__)
# pragma GCC diagnostic pop
#endif

#include "compat.h"
#include "html.h"
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

    lxb_html_tokenizer_keep_duplicate_set(ctx->tokenizer, true);
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

typedef struct reflow_html_open_element_t {
    struct reflow_html_open_element_t *previous;
    lxb_tag_id_t tag_id;
} reflow_html_open_element_t;

typedef struct reflow_html_parse_ctx_t {
    const char *html;
    size_t html_len;
    const reflow_html_handler_t *handler;
    reflow_html_open_element_t *open_element;
    size_t pending_text_start;
    size_t pending_text_end;
    int has_pending_text;
} reflow_html_parse_ctx_t;

static int html_handler_is_valid(const reflow_html_handler_t *handler)
{
    return handler != NULL && handler->on_element_begin != NULL &&
           handler->on_attribute != NULL && handler->on_start_tag_end != NULL &&
           handler->on_element_end != NULL && handler->on_text != NULL &&
           handler->on_comment != NULL;
}

static int html_source_offset(const reflow_html_parse_ctx_t *ctx,
                              const lxb_char_t *position, size_t *offset)
{
    uintptr_t base    = (uintptr_t)(const void *)ctx->html;
    uintptr_t address = (uintptr_t)(const void *)position;

    if (position == NULL || address < base || address - base > ctx->html_len) {
        return 0;
    }
    *offset = (size_t)(address - base);
    return 1;
}

static int html_source_span(const reflow_html_parse_ctx_t *ctx,
                            const lxb_char_t *begin, const lxb_char_t *end,
                            size_t *start, size_t *length)
{
    size_t first = 0;
    size_t last  = 0;

    if (!html_source_offset(ctx, begin, &first) ||
        !html_source_offset(ctx, end, &last) || first > last) {
        return 0;
    }
    *start  = first;
    *length = last - first;
    return 1;
}

static int html_borrowed_span(const lxb_char_t *begin, const lxb_char_t *end,
                              const char **data, size_t *length)
{
    uintptr_t first = (uintptr_t)(const void *)begin;
    uintptr_t last  = (uintptr_t)(const void *)end;

    if (begin == NULL || end == NULL || first > last ||
        last - first > SIZE_MAX) {
        return 0;
    }
    *data   = (const char *)begin;
    *length = (size_t)(last - first);
    return 1;
}

static void html_flush_text(reflow_html_parse_ctx_t *ctx)
{
    if (!ctx->has_pending_text) {
        return;
    }
    ctx->handler->on_text(ctx->handler->ctx,
                          ctx->html + ctx->pending_text_start,
                          ctx->pending_text_end - ctx->pending_text_start);
    ctx->has_pending_text = 0;
}

static int html_accumulate_text(reflow_html_parse_ctx_t *ctx,
                                const lxb_html_token_t *token)
{
    size_t start  = 0;
    size_t length = 0;
    size_t end    = 0;

    if (!html_source_span(ctx, token->begin, token->end, &start, &length)) {
        return 0;
    }
    end = start + length;
    if (ctx->has_pending_text && ctx->pending_text_end == start) {
        ctx->pending_text_end = end;
        return 1;
    }
    html_flush_text(ctx);
    ctx->pending_text_start = start;
    ctx->pending_text_end   = end;
    ctx->has_pending_text   = 1;
    return 1;
}

static int html_start_tag_range(const reflow_html_parse_ctx_t *ctx,
                                const lxb_html_token_t *token,
                                size_t *source_start, size_t *source_end)
{
    const lxb_html_token_attr_t *attr = NULL;
    size_t name_start                 = 0;
    size_t cursor                     = 0;
    size_t offset                     = 0;

    if (!html_source_offset(ctx, token->begin, &name_start) ||
        name_start == 0 || ctx->html[name_start - 1] != '<') {
        return 0;
    }
    *source_start = name_start - 1;

    if (!html_source_offset(ctx, token->end, &cursor)) {
        return 0;
    }
    for (attr = token->attr_first; attr != NULL; attr = attr->next) {
        if (html_source_offset(ctx, attr->name_end, &offset) &&
            offset > cursor) {
            cursor = offset;
        }
        if (html_source_offset(ctx, attr->value_end, &offset) &&
            offset > cursor) {
            cursor = offset;
        }
    }
    while (cursor < ctx->html_len && ctx->html[cursor] != '>') {
        cursor++;
    }
    if (cursor < ctx->html_len) {
        cursor++;
    }
    *source_end = cursor;
    return *source_start <= *source_end;
}

static lxb_html_token_t *html_abort(lxb_html_tokenizer_t *tokenizer,
                                    lxb_status_t status)
{
    lxb_html_tokenizer_status_set(tokenizer, status);
    return NULL;
}

static lxb_html_token_t *html_close_elements(lxb_html_tokenizer_t *tokenizer,
                                             lxb_html_token_t *token,
                                             reflow_html_parse_ctx_t *ctx)
{
    reflow_html_open_element_t *match = ctx->open_element;

    while (match != NULL && match->tag_id != token->tag_id) {
        match = match->previous;
    }
    if (match == NULL) {
        return token;
    }

    while (ctx->open_element != match->previous) {
        reflow_html_open_element_t *closed = ctx->open_element;

        ctx->handler->on_element_end(ctx->handler->ctx);
        ctx->open_element = closed->previous;
        if (pool_free(active_pool, closed) != 0) {
            return html_abort(tokenizer, LXB_STATUS_ERROR);
        }
    }
    return token;
}

static lxb_html_token_t *html_open_element(lxb_html_tokenizer_t *tokenizer,
                                           lxb_html_token_t *token,
                                           reflow_html_parse_ctx_t *ctx,
                                           const char *name, size_t name_len)
{
    const lxb_html_token_attr_t *attr = NULL;
    size_t source_start               = 0;
    size_t source_end                 = 0;
    int is_void_element               = lxb_html_tag_is_void(token->tag_id);

    if (!html_start_tag_range(ctx, token, &source_start, &source_end)) {
        return html_abort(tokenizer, LXB_STATUS_ERROR);
    }
    ctx->handler->on_element_begin(ctx->handler->ctx, name, name_len,
                                   source_start, source_end);

    for (attr = token->attr_first; attr != NULL; attr = attr->next) {
        const lxb_char_t *attr_name = NULL;
        const char *value           = "";
        size_t attr_name_len        = 0;
        size_t value_len            = 0;

        attr_name = lxb_html_token_attr_name((lxb_html_token_attr_t *)attr,
                                             &attr_name_len);
        if (attr_name == NULL) {
            return html_abort(tokenizer, LXB_STATUS_ERROR);
        }
        if (attr->value_begin != NULL || attr->value_end != NULL) {
            if (!html_borrowed_span(attr->value_begin, attr->value_end, &value,
                                    &value_len)) {
                return html_abort(tokenizer, LXB_STATUS_ERROR);
            }
        }
        ctx->handler->on_attribute(ctx->handler->ctx, (const char *)attr_name,
                                   attr_name_len, value, value_len);
    }

    ctx->handler->on_start_tag_end(ctx->handler->ctx, is_void_element);
    if (!is_void_element) {
        reflow_html_open_element_t *opened =
            (reflow_html_open_element_t *)pool_alloc(active_pool,
                                                     sizeof(*opened));
        if (opened == NULL) {
            return html_abort(tokenizer, LXB_STATUS_ERROR_MEMORY_ALLOCATION);
        }
        *opened = (reflow_html_open_element_t){
            .previous = ctx->open_element,
            .tag_id   = token->tag_id,
        };
        ctx->open_element = opened;
    }
    lxb_html_tokenizer_set_state_by_tag(tokenizer, false, token->tag_id,
                                        LXB_NS_HTML);
    return token;
}

static lxb_html_token_t *html_token_done(lxb_html_tokenizer_t *tokenizer,
                                         lxb_html_token_t *token, void *data)
{
    reflow_html_parse_ctx_t *ctx = (reflow_html_parse_ctx_t *)data;
    const lxb_char_t *tag_name   = NULL;
    size_t tag_name_len          = 0;

    if (token->tag_id == LXB_TAG__TEXT) {
        if (!html_accumulate_text(ctx, token)) {
            return html_abort(tokenizer, LXB_STATUS_ERROR);
        }
        return token;
    }

    html_flush_text(ctx);
    switch (token->tag_id) {
    case LXB_TAG__EM_COMMENT: {
        const char *comment = NULL;
        size_t comment_len  = 0;

        if (!html_borrowed_span(token->text_start, token->text_end, &comment,
                                &comment_len)) {
            return html_abort(tokenizer, LXB_STATUS_ERROR);
        }
        ctx->handler->on_comment(ctx->handler->ctx, comment, comment_len);
        return token;
    }
    case LXB_TAG__EM_DOCTYPE:
    case LXB_TAG__END_OF_FILE:
    case LXB_TAG__DOCUMENT:
    case LXB_TAG__UNDEF:
        return token;
    default:
        break;
    }

    tag_name = lxb_tag_name_by_id(token->tag_id, &tag_name_len);
    if (tag_name == NULL) {
        return html_abort(tokenizer, LXB_STATUS_ERROR);
    }
    if ((token->type & LXB_HTML_TOKEN_TYPE_CLOSE) != 0) {
        return html_close_elements(tokenizer, token, ctx);
    }
    return html_open_element(tokenizer, token, ctx, (const char *)tag_name,
                             tag_name_len);
}

int reflow_html_parse(lua_State *L, const char *html, size_t html_len,
                      const reflow_html_handler_t *handler)
{
    reflow_html_parse_ctx_t ctx = {0};
    lxb_status_t lexbor_status  = LXB_STATUS_OK;
    int status                  = LUA_OK;

    if (L == NULL || !html_handler_is_valid(handler) ||
        (html == NULL && html_len != 0)) {
        errno = EINVAL;
        if (L != NULL) {
            lua_pushliteral(L, "invalid HTML parser arguments");
        }
        return LUA_ERRRUN;
    }

    ctx = (reflow_html_parse_ctx_t){
        .html     = html == NULL ? "" : html,
        .html_len = html_len,
        .handler  = handler,
    };
    status = reflow_html_lexbor_tokenize(L, ctx.html, ctx.html_len,
                                         html_token_done, &ctx, &lexbor_status);
    if (status != LUA_OK) {
        return status;
    } else if (lexbor_status != LXB_STATUS_OK) {
        lua_pushfstring(L, "HTML tokenization failed with status %d",
                        (int)lexbor_status);
        return LUA_ERRRUN;
    }
    return LUA_OK;
}
